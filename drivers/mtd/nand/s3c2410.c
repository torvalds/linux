/* linux/drivers/mtd/nand/s3c2410.c
 *
 * Copyright © 2004-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2410/S3C2440/S3C2412 NAND driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef CONFIG_MTD_NAND_S3C2410_DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#include <plat/regs-nand.h>
#include <plat/nand.h>

#ifdef CONFIG_MTD_NAND_S3C2410_HWECC
static int hardware_ecc = 1;
#else
static int hardware_ecc = 0;
#endif

#ifdef CONFIG_MTD_NAND_S3C2410_CLKSTOP
static int clock_stop = 1;
#else
static const int clock_stop = 0;
#endif


/* new oob placement block for use with hardware ecc generation
 */

static struct nand_ecclayout nand_hw_eccoob = {
	.eccbytes = 3,
	.eccpos = {0, 1, 2},
	.oobfree = {{8, 8}}
};

/* controller and mtd information */

struct s3c2410_nand_info;

/**
 * struct s3c2410_nand_mtd - driver MTD structure
 * @mtd: The MTD instance to pass to the MTD layer.
 * @chip: The NAND chip information.
 * @set: The platform information supplied for this set of NAND chips.
 * @info: Link back to the hardware information.
 * @scan_res: The result from calling nand_scan_ident().
*/
struct s3c2410_nand_mtd {
	struct mtd_info			mtd;
	struct nand_chip		chip;
	struct s3c2410_nand_set		*set;
	struct s3c2410_nand_info	*info;
	int				scan_res;
};

enum s3c_cpu_type {
	TYPE_S3C2410,
	TYPE_S3C2412,
	TYPE_S3C2440,
};

/* overview of the s3c2410 nand state */

/**
 * struct s3c2410_nand_info - NAND controller state.
 * @mtds: An array of MTD instances on this controoler.
 * @platform: The platform data for this board.
 * @device: The platform device we bound to.
 * @area: The IO area resource that came from request_mem_region().
 * @clk: The clock resource for this controller.
 * @regs: The area mapped for the hardware registers described by @area.
 * @sel_reg: Pointer to the register controlling the NAND selection.
 * @sel_bit: The bit in @sel_reg to select the NAND chip.
 * @mtd_count: The number of MTDs created from this controller.
 * @save_sel: The contents of @sel_reg to be saved over suspend.
 * @clk_rate: The clock rate from @clk.
 * @cpu_type: The exact type of this controller.
 */
struct s3c2410_nand_info {
	/* mtd info */
	struct nand_hw_control		controller;
	struct s3c2410_nand_mtd		*mtds;
	struct s3c2410_platform_nand	*platform;

	/* device info */
	struct device			*device;
	struct resource			*area;
	struct clk			*clk;
	void __iomem			*regs;
	void __iomem			*sel_reg;
	int				sel_bit;
	int				mtd_count;
	unsigned long			save_sel;
	unsigned long			clk_rate;

	enum s3c_cpu_type		cpu_type;

#ifdef CONFIG_CPU_FREQ
	struct notifier_block	freq_transition;
#endif
};

/* conversion functions */

static struct s3c2410_nand_mtd *s3c2410_nand_mtd_toours(struct mtd_info *mtd)
{
	return container_of(mtd, struct s3c2410_nand_mtd, mtd);
}

static struct s3c2410_nand_info *s3c2410_nand_mtd_toinfo(struct mtd_info *mtd)
{
	return s3c2410_nand_mtd_toours(mtd)->info;
}

static struct s3c2410_nand_info *to_nand_info(struct platform_device *dev)
{
	return platform_get_drvdata(dev);
}

static struct s3c2410_platform_nand *to_nand_plat(struct platform_device *dev)
{
	return dev->dev.platform_data;
}

static inline int allow_clk_stop(struct s3c2410_nand_info *info)
{
	return clock_stop;
}

/* timing calculations */

#define NS_IN_KHZ 1000000

/**
 * s3c_nand_calc_rate - calculate timing data.
 * @wanted: The cycle time in nanoseconds.
 * @clk: The clock rate in kHz.
 * @max: The maximum divider value.
 *
 * Calculate the timing value from the given parameters.
 */
static int s3c_nand_calc_rate(int wanted, unsigned long clk, int max)
{
	int result;

	result = DIV_ROUND_UP((wanted * clk), NS_IN_KHZ);

	pr_debug("result %d from %ld, %d\n", result, clk, wanted);

	if (result > max) {
		printk("%d ns is too big for current clock rate %ld\n", wanted, clk);
		return -1;
	}

	if (result < 1)
		result = 1;

	return result;
}

#define to_ns(ticks,clk) (((ticks) * NS_IN_KHZ) / (unsigned int)(clk))

/* controller setup */

/**
 * s3c2410_nand_setrate - setup controller timing information.
 * @info: The controller instance.
 *
 * Given the information supplied by the platform, calculate and set
 * the necessary timing registers in the hardware to generate the
 * necessary timing cycles to the hardware.
 */
static int s3c2410_nand_setrate(struct s3c2410_nand_info *info)
{
	struct s3c2410_platform_nand *plat = info->platform;
	int tacls_max = (info->cpu_type == TYPE_S3C2412) ? 8 : 4;
	int tacls, twrph0, twrph1;
	unsigned long clkrate = clk_get_rate(info->clk);
	unsigned long uninitialized_var(set), cfg, uninitialized_var(mask);
	unsigned long flags;

	/* calculate the timing information for the controller */

	info->clk_rate = clkrate;
	clkrate /= 1000;	/* turn clock into kHz for ease of use */

	if (plat != NULL) {
		tacls = s3c_nand_calc_rate(plat->tacls, clkrate, tacls_max);
		twrph0 = s3c_nand_calc_rate(plat->twrph0, clkrate, 8);
		twrph1 = s3c_nand_calc_rate(plat->twrph1, clkrate, 8);
	} else {
		/* default timings */
		tacls = tacls_max;
		twrph0 = 8;
		twrph1 = 8;
	}

	if (tacls < 0 || twrph0 < 0 || twrph1 < 0) {
		dev_err(info->device, "cannot get suitable timings\n");
		return -EINVAL;
	}

	dev_info(info->device, "Tacls=%d, %dns Twrph0=%d %dns, Twrph1=%d %dns\n",
	       tacls, to_ns(tacls, clkrate), twrph0, to_ns(twrph0, clkrate), twrph1, to_ns(twrph1, clkrate));

	switch (info->cpu_type) {
	case TYPE_S3C2410:
		mask = (S3C2410_NFCONF_TACLS(3) |
			S3C2410_NFCONF_TWRPH0(7) |
			S3C2410_NFCONF_TWRPH1(7));
		set = S3C2410_NFCONF_EN;
		set |= S3C2410_NFCONF_TACLS(tacls - 1);
		set |= S3C2410_NFCONF_TWRPH0(twrph0 - 1);
		set |= S3C2410_NFCONF_TWRPH1(twrph1 - 1);
		break;

	case TYPE_S3C2440:
	case TYPE_S3C2412:
		mask = (S3C2440_NFCONF_TACLS(tacls_max - 1) |
			S3C2440_NFCONF_TWRPH0(7) |
			S3C2440_NFCONF_TWRPH1(7));

		set = S3C2440_NFCONF_TACLS(tacls - 1);
		set |= S3C2440_NFCONF_TWRPH0(twrph0 - 1);
		set |= S3C2440_NFCONF_TWRPH1(twrph1 - 1);
		break;

	default:
		BUG();
	}

	local_irq_save(flags);

	cfg = readl(info->regs + S3C2410_NFCONF);
	cfg &= ~mask;
	cfg |= set;
	writel(cfg, info->regs + S3C2410_NFCONF);

	local_irq_restore(flags);

	dev_dbg(info->device, "NF_CONF is 0x%lx\n", cfg);

	return 0;
}

/**
 * s3c2410_nand_inithw - basic hardware initialisation
 * @info: The hardware state.
 *
 * Do the basic initialisation of the hardware, using s3c2410_nand_setrate()
 * to setup the hardware access speeds and set the controller to be enabled.
*/
static int s3c2410_nand_inithw(struct s3c2410_nand_info *info)
{
	int ret;

	ret = s3c2410_nand_setrate(info);
	if (ret < 0)
		return ret;

 	switch (info->cpu_type) {
 	case TYPE_S3C2410:
	default:
		break;

 	case TYPE_S3C2440:
 	case TYPE_S3C2412:
		/* enable the controller and de-assert nFCE */

		writel(S3C2440_NFCONT_ENABLE, info->regs + S3C2440_NFCONT);
	}

	return 0;
}

/**
 * s3c2410_nand_select_chip - select the given nand chip
 * @mtd: The MTD instance for this chip.
 * @chip: The chip number.
 *
 * This is called by the MTD layer to either select a given chip for the
 * @mtd instance, or to indicate that the access has finished and the
 * chip can be de-selected.
 *
 * The routine ensures that the nFCE line is correctly setup, and any
 * platform specific selection code is called to route nFCE to the specific
 * chip.
 */
static void s3c2410_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct s3c2410_nand_info *info;
	struct s3c2410_nand_mtd *nmtd;
	struct nand_chip *this = mtd->priv;
	unsigned long cur;

	nmtd = this->priv;
	info = nmtd->info;

	if (chip != -1 && allow_clk_stop(info))
		clk_enable(info->clk);

	cur = readl(info->sel_reg);

	if (chip == -1) {
		cur |= info->sel_bit;
	} else {
		if (nmtd->set != NULL && chip > nmtd->set->nr_chips) {
			dev_err(info->device, "invalid chip %d\n", chip);
			return;
		}

		if (info->platform != NULL) {
			if (info->platform->select_chip != NULL)
				(info->platform->select_chip) (nmtd->set, chip);
		}

		cur &= ~info->sel_bit;
	}

	writel(cur, info->sel_reg);

	if (chip == -1 && allow_clk_stop(info))
		clk_disable(info->clk);
}

/* s3c2410_nand_hwcontrol
 *
 * Issue command and address cycles to the chip
*/

static void s3c2410_nand_hwcontrol(struct mtd_info *mtd, int cmd,
				   unsigned int ctrl)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, info->regs + S3C2410_NFCMD);
	else
		writeb(cmd, info->regs + S3C2410_NFADDR);
}

/* command and control functions */

static void s3c2440_nand_hwcontrol(struct mtd_info *mtd, int cmd,
				   unsigned int ctrl)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, info->regs + S3C2440_NFCMD);
	else
		writeb(cmd, info->regs + S3C2440_NFADDR);
}

/* s3c2410_nand_devready()
 *
 * returns 0 if the nand is busy, 1 if it is ready
*/

static int s3c2410_nand_devready(struct mtd_info *mtd)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	return readb(info->regs + S3C2410_NFSTAT) & S3C2410_NFSTAT_BUSY;
}

static int s3c2440_nand_devready(struct mtd_info *mtd)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	return readb(info->regs + S3C2440_NFSTAT) & S3C2440_NFSTAT_READY;
}

static int s3c2412_nand_devready(struct mtd_info *mtd)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	return readb(info->regs + S3C2412_NFSTAT) & S3C2412_NFSTAT_READY;
}

/* ECC handling functions */

static int s3c2410_nand_correct_data(struct mtd_info *mtd, u_char *dat,
				     u_char *read_ecc, u_char *calc_ecc)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	unsigned int diff0, diff1, diff2;
	unsigned int bit, byte;

	pr_debug("%s(%p,%p,%p,%p)\n", __func__, mtd, dat, read_ecc, calc_ecc);

	diff0 = read_ecc[0] ^ calc_ecc[0];
	diff1 = read_ecc[1] ^ calc_ecc[1];
	diff2 = read_ecc[2] ^ calc_ecc[2];

	pr_debug("%s: rd %02x%02x%02x calc %02x%02x%02x diff %02x%02x%02x\n",
		 __func__,
		 read_ecc[0], read_ecc[1], read_ecc[2],
		 calc_ecc[0], calc_ecc[1], calc_ecc[2],
		 diff0, diff1, diff2);

	if (diff0 == 0 && diff1 == 0 && diff2 == 0)
		return 0;		/* ECC is ok */

	/* sometimes people do not think about using the ECC, so check
	 * to see if we have an 0xff,0xff,0xff read ECC and then ignore
	 * the error, on the assumption that this is an un-eccd page.
	 */
	if (read_ecc[0] == 0xff && read_ecc[1] == 0xff && read_ecc[2] == 0xff
	    && info->platform->ignore_unset_ecc)
		return 0;

	/* Can we correct this ECC (ie, one row and column change).
	 * Note, this is similar to the 256 error code on smartmedia */

	if (((diff0 ^ (diff0 >> 1)) & 0x55) == 0x55 &&
	    ((diff1 ^ (diff1 >> 1)) & 0x55) == 0x55 &&
	    ((diff2 ^ (diff2 >> 1)) & 0x55) == 0x55) {
		/* calculate the bit position of the error */

		bit  = ((diff2 >> 3) & 1) |
		       ((diff2 >> 4) & 2) |
		       ((diff2 >> 5) & 4);

		/* calculate the byte position of the error */

		byte = ((diff2 << 7) & 0x100) |
		       ((diff1 << 0) & 0x80)  |
		       ((diff1 << 1) & 0x40)  |
		       ((diff1 << 2) & 0x20)  |
		       ((diff1 << 3) & 0x10)  |
		       ((diff0 >> 4) & 0x08)  |
		       ((diff0 >> 3) & 0x04)  |
		       ((diff0 >> 2) & 0x02)  |
		       ((diff0 >> 1) & 0x01);

		dev_dbg(info->device, "correcting error bit %d, byte %d\n",
			bit, byte);

		dat[byte] ^= (1 << bit);
		return 1;
	}

	/* if there is only one bit difference in the ECC, then
	 * one of only a row or column parity has changed, which
	 * means the error is most probably in the ECC itself */

	diff0 |= (diff1 << 8);
	diff0 |= (diff2 << 16);

	if ((diff0 & ~(1<<fls(diff0))) == 0)
		return 1;

	return -1;
}

/* ECC functions
 *
 * These allow the s3c2410 and s3c2440 to use the controller's ECC
 * generator block to ECC the data as it passes through]
*/

static void s3c2410_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	unsigned long ctrl;

	ctrl = readl(info->regs + S3C2410_NFCONF);
	ctrl |= S3C2410_NFCONF_INITECC;
	writel(ctrl, info->regs + S3C2410_NFCONF);
}

static void s3c2412_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	unsigned long ctrl;

	ctrl = readl(info->regs + S3C2440_NFCONT);
	writel(ctrl | S3C2412_NFCONT_INIT_MAIN_ECC, info->regs + S3C2440_NFCONT);
}

static void s3c2440_nand_enable_hwecc(struct mtd_info *mtd, int mode)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	unsigned long ctrl;

	ctrl = readl(info->regs + S3C2440_NFCONT);
	writel(ctrl | S3C2440_NFCONT_INITECC, info->regs + S3C2440_NFCONT);
}

static int s3c2410_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);

	ecc_code[0] = readb(info->regs + S3C2410_NFECC + 0);
	ecc_code[1] = readb(info->regs + S3C2410_NFECC + 1);
	ecc_code[2] = readb(info->regs + S3C2410_NFECC + 2);

	pr_debug("%s: returning ecc %02x%02x%02x\n", __func__,
		 ecc_code[0], ecc_code[1], ecc_code[2]);

	return 0;
}

static int s3c2412_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	unsigned long ecc = readl(info->regs + S3C2412_NFMECC0);

	ecc_code[0] = ecc;
	ecc_code[1] = ecc >> 8;
	ecc_code[2] = ecc >> 16;

	pr_debug("calculate_ecc: returning ecc %02x,%02x,%02x\n", ecc_code[0], ecc_code[1], ecc_code[2]);

	return 0;
}

static int s3c2440_nand_calculate_ecc(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);
	unsigned long ecc = readl(info->regs + S3C2440_NFMECC0);

	ecc_code[0] = ecc;
	ecc_code[1] = ecc >> 8;
	ecc_code[2] = ecc >> 16;

	pr_debug("%s: returning ecc %06lx\n", __func__, ecc & 0xffffff);

	return 0;
}

/* over-ride the standard functions for a little more speed. We can
 * use read/write block to move the data buffers to/from the controller
*/

static void s3c2410_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;
	readsb(this->IO_ADDR_R, buf, len);
}

static void s3c2440_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);

	readsl(info->regs + S3C2440_NFDATA, buf, len >> 2);

	/* cleanup if we've got less than a word to do */
	if (len & 3) {
		buf += len & ~3;

		for (; len & 3; len--)
			*buf++ = readb(info->regs + S3C2440_NFDATA);
	}
}

static void s3c2410_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct nand_chip *this = mtd->priv;
	writesb(this->IO_ADDR_W, buf, len);
}

static void s3c2440_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct s3c2410_nand_info *info = s3c2410_nand_mtd_toinfo(mtd);

	writesl(info->regs + S3C2440_NFDATA, buf, len >> 2);

	/* cleanup any fractional write */
	if (len & 3) {
		buf += len & ~3;

		for (; len & 3; len--, buf++)
			writeb(*buf, info->regs + S3C2440_NFDATA);
	}
}

/* cpufreq driver support */

#ifdef CONFIG_CPU_FREQ

static int s3c2410_nand_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	struct s3c2410_nand_info *info;
	unsigned long newclk;

	info = container_of(nb, struct s3c2410_nand_info, freq_transition);
	newclk = clk_get_rate(info->clk);

	if ((val == CPUFREQ_POSTCHANGE && newclk < info->clk_rate) ||
	    (val == CPUFREQ_PRECHANGE && newclk > info->clk_rate)) {
		s3c2410_nand_setrate(info);
	}

	return 0;
}

static inline int s3c2410_nand_cpufreq_register(struct s3c2410_nand_info *info)
{
	info->freq_transition.notifier_call = s3c2410_nand_cpufreq_transition;

	return cpufreq_register_notifier(&info->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void s3c2410_nand_cpufreq_deregister(struct s3c2410_nand_info *info)
{
	cpufreq_unregister_notifier(&info->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int s3c2410_nand_cpufreq_register(struct s3c2410_nand_info *info)
{
	return 0;
}

static inline void s3c2410_nand_cpufreq_deregister(struct s3c2410_nand_info *info)
{
}
#endif

/* device management functions */

static int s3c24xx_nand_remove(struct platform_device *pdev)
{
	struct s3c2410_nand_info *info = to_nand_info(pdev);

	platform_set_drvdata(pdev, NULL);

	if (info == NULL)
		return 0;

	s3c2410_nand_cpufreq_deregister(info);

	/* Release all our mtds  and their partitions, then go through
	 * freeing the resources used
	 */

	if (info->mtds != NULL) {
		struct s3c2410_nand_mtd *ptr = info->mtds;
		int mtdno;

		for (mtdno = 0; mtdno < info->mtd_count; mtdno++, ptr++) {
			pr_debug("releasing mtd %d (%p)\n", mtdno, ptr);
			nand_release(&ptr->mtd);
		}

		kfree(info->mtds);
	}

	/* free the common resources */

	if (info->clk != NULL && !IS_ERR(info->clk)) {
		if (!allow_clk_stop(info))
			clk_disable(info->clk);
		clk_put(info->clk);
	}

	if (info->regs != NULL) {
		iounmap(info->regs);
		info->regs = NULL;
	}

	if (info->area != NULL) {
		release_resource(info->area);
		kfree(info->area);
		info->area = NULL;
	}

	kfree(info);

	return 0;
}

#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", NULL };
static int s3c2410_nand_add_partition(struct s3c2410_nand_info *info,
				      struct s3c2410_nand_mtd *mtd,
				      struct s3c2410_nand_set *set)
{
	struct mtd_partition *part_info;
	int nr_part = 0;

	if (set == NULL)
		return add_mtd_device(&mtd->mtd);

	if (set->nr_partitions == 0) {
		mtd->mtd.name = set->name;
		nr_part = parse_mtd_partitions(&mtd->mtd, part_probes,
						&part_info, 0);
	} else {
		if (set->nr_partitions > 0 && set->partitions != NULL) {
			nr_part = set->nr_partitions;
			part_info = set->partitions;
		}
	}

	if (nr_part > 0 && part_info)
		return add_mtd_partitions(&mtd->mtd, part_info, nr_part);

	return add_mtd_device(&mtd->mtd);
}
#else
static int s3c2410_nand_add_partition(struct s3c2410_nand_info *info,
				      struct s3c2410_nand_mtd *mtd,
				      struct s3c2410_nand_set *set)
{
	return add_mtd_device(&mtd->mtd);
}
#endif

/**
 * s3c2410_nand_init_chip - initialise a single instance of an chip
 * @info: The base NAND controller the chip is on.
 * @nmtd: The new controller MTD instance to fill in.
 * @set: The information passed from the board specific platform data.
 *
 * Initialise the given @nmtd from the information in @info and @set. This
 * readies the structure for use with the MTD layer functions by ensuring
 * all pointers are setup and the necessary control routines selected.
 */
static void s3c2410_nand_init_chip(struct s3c2410_nand_info *info,
				   struct s3c2410_nand_mtd *nmtd,
				   struct s3c2410_nand_set *set)
{
	struct nand_chip *chip = &nmtd->chip;
	void __iomem *regs = info->regs;

	chip->write_buf    = s3c2410_nand_write_buf;
	chip->read_buf     = s3c2410_nand_read_buf;
	chip->select_chip  = s3c2410_nand_select_chip;
	chip->chip_delay   = 50;
	chip->priv	   = nmtd;
	chip->options	   = set->options;
	chip->controller   = &info->controller;

	switch (info->cpu_type) {
	case TYPE_S3C2410:
		chip->IO_ADDR_W = regs + S3C2410_NFDATA;
		info->sel_reg   = regs + S3C2410_NFCONF;
		info->sel_bit	= S3C2410_NFCONF_nFCE;
		chip->cmd_ctrl  = s3c2410_nand_hwcontrol;
		chip->dev_ready = s3c2410_nand_devready;
		break;

	case TYPE_S3C2440:
		chip->IO_ADDR_W = regs + S3C2440_NFDATA;
		info->sel_reg   = regs + S3C2440_NFCONT;
		info->sel_bit	= S3C2440_NFCONT_nFCE;
		chip->cmd_ctrl  = s3c2440_nand_hwcontrol;
		chip->dev_ready = s3c2440_nand_devready;
		chip->read_buf  = s3c2440_nand_read_buf;
		chip->write_buf	= s3c2440_nand_write_buf;
		break;

	case TYPE_S3C2412:
		chip->IO_ADDR_W = regs + S3C2440_NFDATA;
		info->sel_reg   = regs + S3C2440_NFCONT;
		info->sel_bit	= S3C2412_NFCONT_nFCE0;
		chip->cmd_ctrl  = s3c2440_nand_hwcontrol;
		chip->dev_ready = s3c2412_nand_devready;

		if (readl(regs + S3C2410_NFCONF) & S3C2412_NFCONF_NANDBOOT)
			dev_info(info->device, "System booted from NAND\n");

		break;
  	}

	chip->IO_ADDR_R = chip->IO_ADDR_W;

	nmtd->info	   = info;
	nmtd->mtd.priv	   = chip;
	nmtd->mtd.owner    = THIS_MODULE;
	nmtd->set	   = set;

	if (hardware_ecc) {
		chip->ecc.calculate = s3c2410_nand_calculate_ecc;
		chip->ecc.correct   = s3c2410_nand_correct_data;
		chip->ecc.mode	    = NAND_ECC_HW;

		switch (info->cpu_type) {
		case TYPE_S3C2410:
			chip->ecc.hwctl	    = s3c2410_nand_enable_hwecc;
			chip->ecc.calculate = s3c2410_nand_calculate_ecc;
			break;

		case TYPE_S3C2412:
  			chip->ecc.hwctl     = s3c2412_nand_enable_hwecc;
  			chip->ecc.calculate = s3c2412_nand_calculate_ecc;
			break;

		case TYPE_S3C2440:
  			chip->ecc.hwctl     = s3c2440_nand_enable_hwecc;
  			chip->ecc.calculate = s3c2440_nand_calculate_ecc;
			break;

		}
	} else {
		chip->ecc.mode	    = NAND_ECC_SOFT;
	}

	if (set->ecc_layout != NULL)
		chip->ecc.layout = set->ecc_layout;

	if (set->disable_ecc)
		chip->ecc.mode	= NAND_ECC_NONE;

	switch (chip->ecc.mode) {
	case NAND_ECC_NONE:
		dev_info(info->device, "NAND ECC disabled\n");
		break;
	case NAND_ECC_SOFT:
		dev_info(info->device, "NAND soft ECC\n");
		break;
	case NAND_ECC_HW:
		dev_info(info->device, "NAND hardware ECC\n");
		break;
	default:
		dev_info(info->device, "NAND ECC UNKNOWN\n");
		break;
	}

	/* If you use u-boot BBT creation code, specifying this flag will
	 * let the kernel fish out the BBT from the NAND, and also skip the
	 * full NAND scan that can take 1/2s or so. Little things... */
	if (set->flash_bbt)
		chip->options |= NAND_USE_FLASH_BBT | NAND_SKIP_BBTSCAN;
}

/**
 * s3c2410_nand_update_chip - post probe update
 * @info: The controller instance.
 * @nmtd: The driver version of the MTD instance.
 *
 * This routine is called after the chip probe has successfully completed
 * and the relevant per-chip information updated. This call ensure that
 * we update the internal state accordingly.
 *
 * The internal state is currently limited to the ECC state information.
*/
static void s3c2410_nand_update_chip(struct s3c2410_nand_info *info,
				     struct s3c2410_nand_mtd *nmtd)
{
	struct nand_chip *chip = &nmtd->chip;

	dev_dbg(info->device, "chip %p => page shift %d\n",
		chip, chip->page_shift);

	if (chip->ecc.mode != NAND_ECC_HW)
		return;

		/* change the behaviour depending on wether we are using
		 * the large or small page nand device */

	if (chip->page_shift > 10) {
		chip->ecc.size	    = 256;
		chip->ecc.bytes	    = 3;
	} else {
		chip->ecc.size	    = 512;
		chip->ecc.bytes	    = 3;
		chip->ecc.layout    = &nand_hw_eccoob;
	}
}

/* s3c24xx_nand_probe
 *
 * called by device layer when it finds a device matching
 * one our driver can handled. This code checks to see if
 * it can allocate all necessary resources then calls the
 * nand layer to look for devices
*/
static int s3c24xx_nand_probe(struct platform_device *pdev)
{
	struct s3c2410_platform_nand *plat = to_nand_plat(pdev);
	enum s3c_cpu_type cpu_type; 
	struct s3c2410_nand_info *info;
	struct s3c2410_nand_mtd *nmtd;
	struct s3c2410_nand_set *sets;
	struct resource *res;
	int err = 0;
	int size;
	int nr_sets;
	int setno;

	cpu_type = platform_get_device_id(pdev)->driver_data;

	pr_debug("s3c2410_nand_probe(%p)\n", pdev);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		dev_err(&pdev->dev, "no memory for flash info\n");
		err = -ENOMEM;
		goto exit_error;
	}

	platform_set_drvdata(pdev, info);

	spin_lock_init(&info->controller.lock);
	init_waitqueue_head(&info->controller.wq);

	/* get the clock source and enable it */

	info->clk = clk_get(&pdev->dev, "nand");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		err = -ENOENT;
		goto exit_error;
	}

	clk_enable(info->clk);

	/* allocate and map the resource */

	/* currently we assume we have the one resource */
	res  = pdev->resource;
	size = resource_size(res);

	info->area = request_mem_region(res->start, size, pdev->name);

	if (info->area == NULL) {
		dev_err(&pdev->dev, "cannot reserve register region\n");
		err = -ENOENT;
		goto exit_error;
	}

	info->device     = &pdev->dev;
	info->platform   = plat;
	info->regs       = ioremap(res->start, size);
	info->cpu_type   = cpu_type;

	if (info->regs == NULL) {
		dev_err(&pdev->dev, "cannot reserve register region\n");
		err = -EIO;
		goto exit_error;
	}

	dev_dbg(&pdev->dev, "mapped registers at %p\n", info->regs);

	/* initialise the hardware */

	err = s3c2410_nand_inithw(info);
	if (err != 0)
		goto exit_error;

	sets = (plat != NULL) ? plat->sets : NULL;
	nr_sets = (plat != NULL) ? plat->nr_sets : 1;

	info->mtd_count = nr_sets;

	/* allocate our information */

	size = nr_sets * sizeof(*info->mtds);
	info->mtds = kzalloc(size, GFP_KERNEL);
	if (info->mtds == NULL) {
		dev_err(&pdev->dev, "failed to allocate mtd storage\n");
		err = -ENOMEM;
		goto exit_error;
	}

	/* initialise all possible chips */

	nmtd = info->mtds;

	for (setno = 0; setno < nr_sets; setno++, nmtd++) {
		pr_debug("initialising set %d (%p, info %p)\n", setno, nmtd, info);

		s3c2410_nand_init_chip(info, nmtd, sets);

		nmtd->scan_res = nand_scan_ident(&nmtd->mtd,
						 (sets) ? sets->nr_chips : 1,
						 NULL);

		if (nmtd->scan_res == 0) {
			s3c2410_nand_update_chip(info, nmtd);
			nand_scan_tail(&nmtd->mtd);
			s3c2410_nand_add_partition(info, nmtd, sets);
		}

		if (sets != NULL)
			sets++;
	}

	err = s3c2410_nand_cpufreq_register(info);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to init cpufreq support\n");
		goto exit_error;
	}

	if (allow_clk_stop(info)) {
		dev_info(&pdev->dev, "clock idle support enabled\n");
		clk_disable(info->clk);
	}

	pr_debug("initialised ok\n");
	return 0;

 exit_error:
	s3c24xx_nand_remove(pdev);

	if (err == 0)
		err = -EINVAL;
	return err;
}

/* PM Support */
#ifdef CONFIG_PM

static int s3c24xx_nand_suspend(struct platform_device *dev, pm_message_t pm)
{
	struct s3c2410_nand_info *info = platform_get_drvdata(dev);

	if (info) {
		info->save_sel = readl(info->sel_reg);

		/* For the moment, we must ensure nFCE is high during
		 * the time we are suspended. This really should be
		 * handled by suspending the MTDs we are using, but
		 * that is currently not the case. */

		writel(info->save_sel | info->sel_bit, info->sel_reg);

		if (!allow_clk_stop(info))
			clk_disable(info->clk);
	}

	return 0;
}

static int s3c24xx_nand_resume(struct platform_device *dev)
{
	struct s3c2410_nand_info *info = platform_get_drvdata(dev);
	unsigned long sel;

	if (info) {
		clk_enable(info->clk);
		s3c2410_nand_inithw(info);

		/* Restore the state of the nFCE line. */

		sel = readl(info->sel_reg);
		sel &= ~info->sel_bit;
		sel |= info->save_sel & info->sel_bit;
		writel(sel, info->sel_reg);

		if (allow_clk_stop(info))
			clk_disable(info->clk);
	}

	return 0;
}

#else
#define s3c24xx_nand_suspend NULL
#define s3c24xx_nand_resume NULL
#endif

/* driver device registration */

static struct platform_device_id s3c24xx_driver_ids[] = {
	{
		.name		= "s3c2410-nand",
		.driver_data	= TYPE_S3C2410,
	}, {
		.name		= "s3c2440-nand",
		.driver_data	= TYPE_S3C2440,
	}, {
		.name		= "s3c2412-nand",
		.driver_data	= TYPE_S3C2412,
	}, {
		.name		= "s3c6400-nand",
		.driver_data	= TYPE_S3C2412, /* compatible with 2412 */
	},
	{ }
};

MODULE_DEVICE_TABLE(platform, s3c24xx_driver_ids);

static struct platform_driver s3c24xx_nand_driver = {
	.probe		= s3c24xx_nand_probe,
	.remove		= s3c24xx_nand_remove,
	.suspend	= s3c24xx_nand_suspend,
	.resume		= s3c24xx_nand_resume,
	.id_table	= s3c24xx_driver_ids,
	.driver		= {
		.name	= "s3c24xx-nand",
		.owner	= THIS_MODULE,
	},
};

static int __init s3c2410_nand_init(void)
{
	printk("S3C24XX NAND Driver, (c) 2004 Simtec Electronics\n");

	return platform_driver_register(&s3c24xx_nand_driver);
}

static void __exit s3c2410_nand_exit(void)
{
	platform_driver_unregister(&s3c24xx_nand_driver);
}

module_init(s3c2410_nand_init);
module_exit(s3c2410_nand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("S3C24XX MTD NAND driver");
