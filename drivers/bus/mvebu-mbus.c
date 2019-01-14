/*
 * Address map functions for Marvell EBU SoCs (Kirkwood, Armada
 * 370/XP, Dove, Orion5x and MV78xx0)
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The Marvell EBU SoCs have a configurable physical address space:
 * the physical address at which certain devices (PCIe, NOR, NAND,
 * etc.) sit can be configured. The configuration takes place through
 * two sets of registers:
 *
 * - One to configure the access of the CPU to the devices. Depending
 *   on the families, there are between 8 and 20 configurable windows,
 *   each can be use to create a physical memory window that maps to a
 *   specific device. Devices are identified by a tuple (target,
 *   attribute).
 *
 * - One to configure the access to the CPU to the SDRAM. There are
 *   either 2 (for Dove) or 4 (for other families) windows to map the
 *   SDRAM into the physical address space.
 *
 * This driver:
 *
 * - Reads out the SDRAM address decoding windows at initialization
 *   time, and fills the mvebu_mbus_dram_info structure with these
 *   informations. The exported function mv_mbus_dram_info() allow
 *   device drivers to get those informations related to the SDRAM
 *   address decoding windows. This is because devices also have their
 *   own windows (configured through registers that are part of each
 *   device register space), and therefore the drivers for Marvell
 *   devices have to configure those device -> SDRAM windows to ensure
 *   that DMA works properly.
 *
 * - Provides an API for platform code or device drivers to
 *   dynamically add or remove address decoding windows for the CPU ->
 *   device accesses. This API is mvebu_mbus_add_window_by_id(),
 *   mvebu_mbus_add_window_remap_by_id() and
 *   mvebu_mbus_del_window().
 *
 * - Provides a debugfs interface in /sys/kernel/debug/mvebu-mbus/ to
 *   see the list of CPU -> SDRAM windows and their configuration
 *   (file 'sdram') and the list of CPU -> devices windows and their
 *   configuration (file 'devices').
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/debugfs.h>
#include <linux/log2.h>
#include <linux/memblock.h>
#include <linux/syscore_ops.h>

/*
 * DDR target is the same on all platforms.
 */
#define TARGET_DDR		0

/*
 * CPU Address Decode Windows registers
 */
#define WIN_CTRL_OFF		0x0000
#define   WIN_CTRL_ENABLE       BIT(0)
/* Only on HW I/O coherency capable platforms */
#define   WIN_CTRL_SYNCBARRIER  BIT(1)
#define   WIN_CTRL_TGT_MASK     0xf0
#define   WIN_CTRL_TGT_SHIFT    4
#define   WIN_CTRL_ATTR_MASK    0xff00
#define   WIN_CTRL_ATTR_SHIFT   8
#define   WIN_CTRL_SIZE_MASK    0xffff0000
#define   WIN_CTRL_SIZE_SHIFT   16
#define WIN_BASE_OFF		0x0004
#define   WIN_BASE_LOW          0xffff0000
#define   WIN_BASE_HIGH         0xf
#define WIN_REMAP_LO_OFF	0x0008
#define   WIN_REMAP_LOW         0xffff0000
#define WIN_REMAP_HI_OFF	0x000c

#define UNIT_SYNC_BARRIER_OFF   0x84
#define   UNIT_SYNC_BARRIER_ALL 0xFFFF

#define ATTR_HW_COHERENCY	(0x1 << 4)

#define DDR_BASE_CS_OFF(n)	(0x0000 + ((n) << 3))
#define  DDR_BASE_CS_HIGH_MASK  0xf
#define  DDR_BASE_CS_LOW_MASK   0xff000000
#define DDR_SIZE_CS_OFF(n)	(0x0004 + ((n) << 3))
#define  DDR_SIZE_ENABLED       BIT(0)
#define  DDR_SIZE_CS_MASK       0x1c
#define  DDR_SIZE_CS_SHIFT      2
#define  DDR_SIZE_MASK          0xff000000

#define DOVE_DDR_BASE_CS_OFF(n) ((n) << 4)

/* Relative to mbusbridge_base */
#define MBUS_BRIDGE_CTRL_OFF	0x0
#define MBUS_BRIDGE_BASE_OFF	0x4

/* Maximum number of windows, for all known platforms */
#define MBUS_WINS_MAX           20

struct mvebu_mbus_state;

struct mvebu_mbus_soc_data {
	unsigned int num_wins;
	bool has_mbus_bridge;
	unsigned int (*win_cfg_offset)(const int win);
	unsigned int (*win_remap_offset)(const int win);
	void (*setup_cpu_target)(struct mvebu_mbus_state *s);
	int (*save_cpu_target)(struct mvebu_mbus_state *s,
			       u32 __iomem *store_addr);
	int (*show_cpu_target)(struct mvebu_mbus_state *s,
			       struct seq_file *seq, void *v);
};

/*
 * Used to store the state of one MBus window accross suspend/resume.
 */
struct mvebu_mbus_win_data {
	u32 ctrl;
	u32 base;
	u32 remap_lo;
	u32 remap_hi;
};

struct mvebu_mbus_state {
	void __iomem *mbuswins_base;
	void __iomem *sdramwins_base;
	void __iomem *mbusbridge_base;
	phys_addr_t sdramwins_phys_base;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sdram;
	struct dentry *debugfs_devs;
	struct resource pcie_mem_aperture;
	struct resource pcie_io_aperture;
	const struct mvebu_mbus_soc_data *soc;
	int hw_io_coherency;

	/* Used during suspend/resume */
	u32 mbus_bridge_ctrl;
	u32 mbus_bridge_base;
	struct mvebu_mbus_win_data wins[MBUS_WINS_MAX];
};

static struct mvebu_mbus_state mbus_state;

/*
 * We provide two variants of the mv_mbus_dram_info() function:
 *
 * - The normal one, where the described DRAM ranges may overlap with
 *   the I/O windows, but for which the DRAM ranges are guaranteed to
 *   have a power of two size. Such ranges are suitable for the DMA
 *   masters that only DMA between the RAM and the device, which is
 *   actually all devices except the crypto engines.
 *
 * - The 'nooverlap' one, where the described DRAM ranges are
 *   guaranteed to not overlap with the I/O windows, but for which the
 *   DRAM ranges will not have power of two sizes. They will only be
 *   aligned on a 64 KB boundary, and have a size multiple of 64
 *   KB. Such ranges are suitable for the DMA masters that DMA between
 *   the crypto SRAM (which is mapped through an I/O window) and a
 *   device. This is the case for the crypto engines.
 */

static struct mbus_dram_target_info mvebu_mbus_dram_info;
static struct mbus_dram_target_info mvebu_mbus_dram_info_nooverlap;

const struct mbus_dram_target_info *mv_mbus_dram_info(void)
{
	return &mvebu_mbus_dram_info;
}
EXPORT_SYMBOL_GPL(mv_mbus_dram_info);

const struct mbus_dram_target_info *mv_mbus_dram_info_nooverlap(void)
{
	return &mvebu_mbus_dram_info_nooverlap;
}
EXPORT_SYMBOL_GPL(mv_mbus_dram_info_nooverlap);

/* Checks whether the given window has remap capability */
static bool mvebu_mbus_window_is_remappable(struct mvebu_mbus_state *mbus,
					    const int win)
{
	return mbus->soc->win_remap_offset(win) != MVEBU_MBUS_NO_REMAP;
}

/*
 * Functions to manipulate the address decoding windows
 */

static void mvebu_mbus_read_window(struct mvebu_mbus_state *mbus,
				   int win, int *enabled, u64 *base,
				   u32 *size, u8 *target, u8 *attr,
				   u64 *remap)
{
	void __iomem *addr = mbus->mbuswins_base +
		mbus->soc->win_cfg_offset(win);
	u32 basereg = readl(addr + WIN_BASE_OFF);
	u32 ctrlreg = readl(addr + WIN_CTRL_OFF);

	if (!(ctrlreg & WIN_CTRL_ENABLE)) {
		*enabled = 0;
		return;
	}

	*enabled = 1;
	*base = ((u64)basereg & WIN_BASE_HIGH) << 32;
	*base |= (basereg & WIN_BASE_LOW);
	*size = (ctrlreg | ~WIN_CTRL_SIZE_MASK) + 1;

	if (target)
		*target = (ctrlreg & WIN_CTRL_TGT_MASK) >> WIN_CTRL_TGT_SHIFT;

	if (attr)
		*attr = (ctrlreg & WIN_CTRL_ATTR_MASK) >> WIN_CTRL_ATTR_SHIFT;

	if (remap) {
		if (mvebu_mbus_window_is_remappable(mbus, win)) {
			u32 remap_low, remap_hi;
			void __iomem *addr_rmp = mbus->mbuswins_base +
				mbus->soc->win_remap_offset(win);
			remap_low = readl(addr_rmp + WIN_REMAP_LO_OFF);
			remap_hi  = readl(addr_rmp + WIN_REMAP_HI_OFF);
			*remap = ((u64)remap_hi << 32) | remap_low;
		} else
			*remap = 0;
	}
}

static void mvebu_mbus_disable_window(struct mvebu_mbus_state *mbus,
				      int win)
{
	void __iomem *addr;

	addr = mbus->mbuswins_base + mbus->soc->win_cfg_offset(win);
	writel(0, addr + WIN_BASE_OFF);
	writel(0, addr + WIN_CTRL_OFF);

	if (mvebu_mbus_window_is_remappable(mbus, win)) {
		addr = mbus->mbuswins_base + mbus->soc->win_remap_offset(win);
		writel(0, addr + WIN_REMAP_LO_OFF);
		writel(0, addr + WIN_REMAP_HI_OFF);
	}
}

/* Checks whether the given window number is available */

static int mvebu_mbus_window_is_free(struct mvebu_mbus_state *mbus,
				     const int win)
{
	void __iomem *addr = mbus->mbuswins_base +
		mbus->soc->win_cfg_offset(win);
	u32 ctrl = readl(addr + WIN_CTRL_OFF);

	return !(ctrl & WIN_CTRL_ENABLE);
}

/*
 * Checks whether the given (base, base+size) area doesn't overlap an
 * existing region
 */
static int mvebu_mbus_window_conflicts(struct mvebu_mbus_state *mbus,
				       phys_addr_t base, size_t size,
				       u8 target, u8 attr)
{
	u64 end = (u64)base + size;
	int win;

	for (win = 0; win < mbus->soc->num_wins; win++) {
		u64 wbase, wend;
		u32 wsize;
		u8 wtarget, wattr;
		int enabled;

		mvebu_mbus_read_window(mbus, win,
				       &enabled, &wbase, &wsize,
				       &wtarget, &wattr, NULL);

		if (!enabled)
			continue;

		wend = wbase + wsize;

		/*
		 * Check if the current window overlaps with the
		 * proposed physical range
		 */
		if ((u64)base < wend && end > wbase)
			return 0;
	}

	return 1;
}

static int mvebu_mbus_find_window(struct mvebu_mbus_state *mbus,
				  phys_addr_t base, size_t size)
{
	int win;

	for (win = 0; win < mbus->soc->num_wins; win++) {
		u64 wbase;
		u32 wsize;
		int enabled;

		mvebu_mbus_read_window(mbus, win,
				       &enabled, &wbase, &wsize,
				       NULL, NULL, NULL);

		if (!enabled)
			continue;

		if (base == wbase && size == wsize)
			return win;
	}

	return -ENODEV;
}

static int mvebu_mbus_setup_window(struct mvebu_mbus_state *mbus,
				   int win, phys_addr_t base, size_t size,
				   phys_addr_t remap, u8 target,
				   u8 attr)
{
	void __iomem *addr = mbus->mbuswins_base +
		mbus->soc->win_cfg_offset(win);
	u32 ctrl, remap_addr;

	if (!is_power_of_2(size)) {
		WARN(true, "Invalid MBus window size: 0x%zx\n", size);
		return -EINVAL;
	}

	if ((base & (phys_addr_t)(size - 1)) != 0) {
		WARN(true, "Invalid MBus base/size: %pa len 0x%zx\n", &base,
		     size);
		return -EINVAL;
	}

	ctrl = ((size - 1) & WIN_CTRL_SIZE_MASK) |
		(attr << WIN_CTRL_ATTR_SHIFT)    |
		(target << WIN_CTRL_TGT_SHIFT)   |
		WIN_CTRL_ENABLE;
	if (mbus->hw_io_coherency)
		ctrl |= WIN_CTRL_SYNCBARRIER;

	writel(base & WIN_BASE_LOW, addr + WIN_BASE_OFF);
	writel(ctrl, addr + WIN_CTRL_OFF);

	if (mvebu_mbus_window_is_remappable(mbus, win)) {
		void __iomem *addr_rmp = mbus->mbuswins_base +
			mbus->soc->win_remap_offset(win);

		if (remap == MVEBU_MBUS_NO_REMAP)
			remap_addr = base;
		else
			remap_addr = remap;
		writel(remap_addr & WIN_REMAP_LOW, addr_rmp + WIN_REMAP_LO_OFF);
		writel(0, addr_rmp + WIN_REMAP_HI_OFF);
	}

	return 0;
}

static int mvebu_mbus_alloc_window(struct mvebu_mbus_state *mbus,
				   phys_addr_t base, size_t size,
				   phys_addr_t remap, u8 target,
				   u8 attr)
{
	int win;

	if (remap == MVEBU_MBUS_NO_REMAP) {
		for (win = 0; win < mbus->soc->num_wins; win++) {
			if (mvebu_mbus_window_is_remappable(mbus, win))
				continue;

			if (mvebu_mbus_window_is_free(mbus, win))
				return mvebu_mbus_setup_window(mbus, win, base,
							       size, remap,
							       target, attr);
		}
	}

	for (win = 0; win < mbus->soc->num_wins; win++) {
		/* Skip window if need remap but is not supported */
		if ((remap != MVEBU_MBUS_NO_REMAP) &&
		    !mvebu_mbus_window_is_remappable(mbus, win))
			continue;

		if (mvebu_mbus_window_is_free(mbus, win))
			return mvebu_mbus_setup_window(mbus, win, base, size,
						       remap, target, attr);
	}

	return -ENOMEM;
}

/*
 * Debugfs debugging
 */

/* Common function used for Dove, Kirkwood, Armada 370/XP and Orion 5x */
static int mvebu_sdram_debug_show_orion(struct mvebu_mbus_state *mbus,
					struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; i < 4; i++) {
		u32 basereg = readl(mbus->sdramwins_base + DDR_BASE_CS_OFF(i));
		u32 sizereg = readl(mbus->sdramwins_base + DDR_SIZE_CS_OFF(i));
		u64 base;
		u32 size;

		if (!(sizereg & DDR_SIZE_ENABLED)) {
			seq_printf(seq, "[%d] disabled\n", i);
			continue;
		}

		base = ((u64)basereg & DDR_BASE_CS_HIGH_MASK) << 32;
		base |= basereg & DDR_BASE_CS_LOW_MASK;
		size = (sizereg | ~DDR_SIZE_MASK);

		seq_printf(seq, "[%d] %016llx - %016llx : cs%d\n",
			   i, (unsigned long long)base,
			   (unsigned long long)base + size + 1,
			   (sizereg & DDR_SIZE_CS_MASK) >> DDR_SIZE_CS_SHIFT);
	}

	return 0;
}

/* Special function for Dove */
static int mvebu_sdram_debug_show_dove(struct mvebu_mbus_state *mbus,
				       struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; i < 2; i++) {
		u32 map = readl(mbus->sdramwins_base + DOVE_DDR_BASE_CS_OFF(i));
		u64 base;
		u32 size;

		if (!(map & 1)) {
			seq_printf(seq, "[%d] disabled\n", i);
			continue;
		}

		base = map & 0xff800000;
		size = 0x100000 << (((map & 0x000f0000) >> 16) - 4);

		seq_printf(seq, "[%d] %016llx - %016llx : cs%d\n",
			   i, (unsigned long long)base,
			   (unsigned long long)base + size, i);
	}

	return 0;
}

static int mvebu_sdram_debug_show(struct seq_file *seq, void *v)
{
	struct mvebu_mbus_state *mbus = &mbus_state;
	return mbus->soc->show_cpu_target(mbus, seq, v);
}

static int mvebu_sdram_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mvebu_sdram_debug_show, inode->i_private);
}

static const struct file_operations mvebu_sdram_debug_fops = {
	.open = mvebu_sdram_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int mvebu_devs_debug_show(struct seq_file *seq, void *v)
{
	struct mvebu_mbus_state *mbus = &mbus_state;
	int win;

	for (win = 0; win < mbus->soc->num_wins; win++) {
		u64 wbase, wremap;
		u32 wsize;
		u8 wtarget, wattr;
		int enabled;

		mvebu_mbus_read_window(mbus, win,
				       &enabled, &wbase, &wsize,
				       &wtarget, &wattr, &wremap);

		if (!enabled) {
			seq_printf(seq, "[%02d] disabled\n", win);
			continue;
		}

		seq_printf(seq, "[%02d] %016llx - %016llx : %04x:%04x",
			   win, (unsigned long long)wbase,
			   (unsigned long long)(wbase + wsize), wtarget, wattr);

		if (!is_power_of_2(wsize) ||
		    ((wbase & (u64)(wsize - 1)) != 0))
			seq_puts(seq, " (Invalid base/size!!)");

		if (mvebu_mbus_window_is_remappable(mbus, win)) {
			seq_printf(seq, " (remap %016llx)\n",
				   (unsigned long long)wremap);
		} else
			seq_printf(seq, "\n");
	}

	return 0;
}

static int mvebu_devs_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mvebu_devs_debug_show, inode->i_private);
}

static const struct file_operations mvebu_devs_debug_fops = {
	.open = mvebu_devs_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * SoC-specific functions and definitions
 */

static unsigned int generic_mbus_win_cfg_offset(int win)
{
	return win << 4;
}

static unsigned int armada_370_xp_mbus_win_cfg_offset(int win)
{
	/* The register layout is a bit annoying and the below code
	 * tries to cope with it.
	 * - At offset 0x0, there are the registers for the first 8
	 *   windows, with 4 registers of 32 bits per window (ctrl,
	 *   base, remap low, remap high)
	 * - Then at offset 0x80, there is a hole of 0x10 bytes for
	 *   the internal registers base address and internal units
	 *   sync barrier register.
	 * - Then at offset 0x90, there the registers for 12
	 *   windows, with only 2 registers of 32 bits per window
	 *   (ctrl, base).
	 */
	if (win < 8)
		return win << 4;
	else
		return 0x90 + ((win - 8) << 3);
}

static unsigned int mv78xx0_mbus_win_cfg_offset(int win)
{
	if (win < 8)
		return win << 4;
	else
		return 0x900 + ((win - 8) << 4);
}

static unsigned int generic_mbus_win_remap_2_offset(int win)
{
	if (win < 2)
		return generic_mbus_win_cfg_offset(win);
	else
		return MVEBU_MBUS_NO_REMAP;
}

static unsigned int generic_mbus_win_remap_4_offset(int win)
{
	if (win < 4)
		return generic_mbus_win_cfg_offset(win);
	else
		return MVEBU_MBUS_NO_REMAP;
}

static unsigned int generic_mbus_win_remap_8_offset(int win)
{
	if (win < 8)
		return generic_mbus_win_cfg_offset(win);
	else
		return MVEBU_MBUS_NO_REMAP;
}

static unsigned int armada_xp_mbus_win_remap_offset(int win)
{
	if (win < 8)
		return generic_mbus_win_cfg_offset(win);
	else if (win == 13)
		return 0xF0 - WIN_REMAP_LO_OFF;
	else
		return MVEBU_MBUS_NO_REMAP;
}

/*
 * Use the memblock information to find the MBus bridge hole in the
 * physical address space.
 */
static void __init
mvebu_mbus_find_bridge_hole(uint64_t *start, uint64_t *end)
{
	struct memblock_region *r;
	uint64_t s = 0;

	for_each_memblock(memory, r) {
		/*
		 * This part of the memory is above 4 GB, so we don't
		 * care for the MBus bridge hole.
		 */
		if (r->base >= 0x100000000ULL)
			continue;

		/*
		 * The MBus bridge hole is at the end of the RAM under
		 * the 4 GB limit.
		 */
		if (r->base + r->size > s)
			s = r->base + r->size;
	}

	*start = s;
	*end = 0x100000000ULL;
}

/*
 * This function fills in the mvebu_mbus_dram_info_nooverlap data
 * structure, by looking at the mvebu_mbus_dram_info data, and
 * removing the parts of it that overlap with I/O windows.
 */
static void __init
mvebu_mbus_setup_cpu_target_nooverlap(struct mvebu_mbus_state *mbus)
{
	uint64_t mbus_bridge_base, mbus_bridge_end;
	int cs_nooverlap = 0;
	int i;

	mvebu_mbus_find_bridge_hole(&mbus_bridge_base, &mbus_bridge_end);

	for (i = 0; i < mvebu_mbus_dram_info.num_cs; i++) {
		struct mbus_dram_window *w;
		u64 base, size, end;

		w = &mvebu_mbus_dram_info.cs[i];
		base = w->base;
		size = w->size;
		end = base + size;

		/*
		 * The CS is fully enclosed inside the MBus bridge
		 * area, so ignore it.
		 */
		if (base >= mbus_bridge_base && end <= mbus_bridge_end)
			continue;

		/*
		 * Beginning of CS overlaps with end of MBus, raise CS
		 * base address, and shrink its size.
		 */
		if (base >= mbus_bridge_base && end > mbus_bridge_end) {
			size -= mbus_bridge_end - base;
			base = mbus_bridge_end;
		}

		/*
		 * End of CS overlaps with beginning of MBus, shrink
		 * CS size.
		 */
		if (base < mbus_bridge_base && end > mbus_bridge_base)
			size -= end - mbus_bridge_base;

		w = &mvebu_mbus_dram_info_nooverlap.cs[cs_nooverlap++];
		w->cs_index = i;
		w->mbus_attr = 0xf & ~(1 << i);
		if (mbus->hw_io_coherency)
			w->mbus_attr |= ATTR_HW_COHERENCY;
		w->base = base;
		w->size = size;
	}

	mvebu_mbus_dram_info_nooverlap.mbus_dram_target_id = TARGET_DDR;
	mvebu_mbus_dram_info_nooverlap.num_cs = cs_nooverlap;
}

static void __init
mvebu_mbus_default_setup_cpu_target(struct mvebu_mbus_state *mbus)
{
	int i;
	int cs;

	mvebu_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 4; i++) {
		u32 base = readl(mbus->sdramwins_base + DDR_BASE_CS_OFF(i));
		u32 size = readl(mbus->sdramwins_base + DDR_SIZE_CS_OFF(i));

		/*
		 * We only take care of entries for which the chip
		 * select is enabled, and that don't have high base
		 * address bits set (devices can only access the first
		 * 32 bits of the memory).
		 */
		if ((size & DDR_SIZE_ENABLED) &&
		    !(base & DDR_BASE_CS_HIGH_MASK)) {
			struct mbus_dram_window *w;

			w = &mvebu_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0xf & ~(1 << i);
			if (mbus->hw_io_coherency)
				w->mbus_attr |= ATTR_HW_COHERENCY;
			w->base = base & DDR_BASE_CS_LOW_MASK;
			w->size = (u64)(size | ~DDR_SIZE_MASK) + 1;
		}
	}
	mvebu_mbus_dram_info.num_cs = cs;
}

static int
mvebu_mbus_default_save_cpu_target(struct mvebu_mbus_state *mbus,
				   u32 __iomem *store_addr)
{
	int i;

	for (i = 0; i < 4; i++) {
		u32 base = readl(mbus->sdramwins_base + DDR_BASE_CS_OFF(i));
		u32 size = readl(mbus->sdramwins_base + DDR_SIZE_CS_OFF(i));

		writel(mbus->sdramwins_phys_base + DDR_BASE_CS_OFF(i),
		       store_addr++);
		writel(base, store_addr++);
		writel(mbus->sdramwins_phys_base + DDR_SIZE_CS_OFF(i),
		       store_addr++);
		writel(size, store_addr++);
	}

	/* We've written 16 words to the store address */
	return 16;
}

static void __init
mvebu_mbus_dove_setup_cpu_target(struct mvebu_mbus_state *mbus)
{
	int i;
	int cs;

	mvebu_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 2; i++) {
		u32 map = readl(mbus->sdramwins_base + DOVE_DDR_BASE_CS_OFF(i));

		/*
		 * Chip select enabled?
		 */
		if (map & 1) {
			struct mbus_dram_window *w;

			w = &mvebu_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0; /* CS address decoding done inside */
					  /* the DDR controller, no need to  */
					  /* provide attributes */
			w->base = map & 0xff800000;
			w->size = 0x100000 << (((map & 0x000f0000) >> 16) - 4);
		}
	}

	mvebu_mbus_dram_info.num_cs = cs;
}

static int
mvebu_mbus_dove_save_cpu_target(struct mvebu_mbus_state *mbus,
				u32 __iomem *store_addr)
{
	int i;

	for (i = 0; i < 2; i++) {
		u32 map = readl(mbus->sdramwins_base + DOVE_DDR_BASE_CS_OFF(i));

		writel(mbus->sdramwins_phys_base + DOVE_DDR_BASE_CS_OFF(i),
		       store_addr++);
		writel(map, store_addr++);
	}

	/* We've written 4 words to the store address */
	return 4;
}

int mvebu_mbus_save_cpu_target(u32 __iomem *store_addr)
{
	return mbus_state.soc->save_cpu_target(&mbus_state, store_addr);
}

static const struct mvebu_mbus_soc_data armada_370_mbus_data = {
	.num_wins            = 20,
	.has_mbus_bridge     = true,
	.win_cfg_offset      = armada_370_xp_mbus_win_cfg_offset,
	.win_remap_offset    = generic_mbus_win_remap_8_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.save_cpu_target     = mvebu_mbus_default_save_cpu_target,
};

static const struct mvebu_mbus_soc_data armada_xp_mbus_data = {
	.num_wins            = 20,
	.has_mbus_bridge     = true,
	.win_cfg_offset      = armada_370_xp_mbus_win_cfg_offset,
	.win_remap_offset    = armada_xp_mbus_win_remap_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.save_cpu_target     = mvebu_mbus_default_save_cpu_target,
};

static const struct mvebu_mbus_soc_data kirkwood_mbus_data = {
	.num_wins            = 8,
	.win_cfg_offset      = generic_mbus_win_cfg_offset,
	.save_cpu_target     = mvebu_mbus_default_save_cpu_target,
	.win_remap_offset    = generic_mbus_win_remap_4_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
};

static const struct mvebu_mbus_soc_data dove_mbus_data = {
	.num_wins            = 8,
	.win_cfg_offset      = generic_mbus_win_cfg_offset,
	.save_cpu_target     = mvebu_mbus_dove_save_cpu_target,
	.win_remap_offset    = generic_mbus_win_remap_4_offset,
	.setup_cpu_target    = mvebu_mbus_dove_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_dove,
};

/*
 * Some variants of Orion5x have 4 remappable windows, some other have
 * only two of them.
 */
static const struct mvebu_mbus_soc_data orion5x_4win_mbus_data = {
	.num_wins            = 8,
	.win_cfg_offset      = generic_mbus_win_cfg_offset,
	.save_cpu_target     = mvebu_mbus_default_save_cpu_target,
	.win_remap_offset    = generic_mbus_win_remap_4_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
};

static const struct mvebu_mbus_soc_data orion5x_2win_mbus_data = {
	.num_wins            = 8,
	.win_cfg_offset      = generic_mbus_win_cfg_offset,
	.save_cpu_target     = mvebu_mbus_default_save_cpu_target,
	.win_remap_offset    = generic_mbus_win_remap_2_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
};

static const struct mvebu_mbus_soc_data mv78xx0_mbus_data = {
	.num_wins            = 14,
	.win_cfg_offset      = mv78xx0_mbus_win_cfg_offset,
	.save_cpu_target     = mvebu_mbus_default_save_cpu_target,
	.win_remap_offset    = generic_mbus_win_remap_8_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
};

static const struct of_device_id of_mvebu_mbus_ids[] = {
	{ .compatible = "marvell,armada370-mbus",
	  .data = &armada_370_mbus_data, },
	{ .compatible = "marvell,armada375-mbus",
	  .data = &armada_xp_mbus_data, },
	{ .compatible = "marvell,armada380-mbus",
	  .data = &armada_xp_mbus_data, },
	{ .compatible = "marvell,armadaxp-mbus",
	  .data = &armada_xp_mbus_data, },
	{ .compatible = "marvell,kirkwood-mbus",
	  .data = &kirkwood_mbus_data, },
	{ .compatible = "marvell,dove-mbus",
	  .data = &dove_mbus_data, },
	{ .compatible = "marvell,orion5x-88f5281-mbus",
	  .data = &orion5x_4win_mbus_data, },
	{ .compatible = "marvell,orion5x-88f5182-mbus",
	  .data = &orion5x_2win_mbus_data, },
	{ .compatible = "marvell,orion5x-88f5181-mbus",
	  .data = &orion5x_2win_mbus_data, },
	{ .compatible = "marvell,orion5x-88f6183-mbus",
	  .data = &orion5x_4win_mbus_data, },
	{ .compatible = "marvell,mv78xx0-mbus",
	  .data = &mv78xx0_mbus_data, },
	{ },
};

/*
 * Public API of the driver
 */
int mvebu_mbus_add_window_remap_by_id(unsigned int target,
				      unsigned int attribute,
				      phys_addr_t base, size_t size,
				      phys_addr_t remap)
{
	struct mvebu_mbus_state *s = &mbus_state;

	if (!mvebu_mbus_window_conflicts(s, base, size, target, attribute)) {
		pr_err("cannot add window '%x:%x', conflicts with another window\n",
		       target, attribute);
		return -EINVAL;
	}

	return mvebu_mbus_alloc_window(s, base, size, remap, target, attribute);
}

int mvebu_mbus_add_window_by_id(unsigned int target, unsigned int attribute,
				phys_addr_t base, size_t size)
{
	return mvebu_mbus_add_window_remap_by_id(target, attribute, base,
						 size, MVEBU_MBUS_NO_REMAP);
}

int mvebu_mbus_del_window(phys_addr_t base, size_t size)
{
	int win;

	win = mvebu_mbus_find_window(&mbus_state, base, size);
	if (win < 0)
		return win;

	mvebu_mbus_disable_window(&mbus_state, win);
	return 0;
}

void mvebu_mbus_get_pcie_mem_aperture(struct resource *res)
{
	if (!res)
		return;
	*res = mbus_state.pcie_mem_aperture;
}

void mvebu_mbus_get_pcie_io_aperture(struct resource *res)
{
	if (!res)
		return;
	*res = mbus_state.pcie_io_aperture;
}

int mvebu_mbus_get_dram_win_info(phys_addr_t phyaddr, u8 *target, u8 *attr)
{
	const struct mbus_dram_target_info *dram;
	int i;

	/* Get dram info */
	dram = mv_mbus_dram_info();
	if (!dram) {
		pr_err("missing DRAM information\n");
		return -ENODEV;
	}

	/* Try to find matching DRAM window for phyaddr */
	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		if (cs->base <= phyaddr &&
			phyaddr <= (cs->base + cs->size - 1)) {
			*target = dram->mbus_dram_target_id;
			*attr = cs->mbus_attr;
			return 0;
		}
	}

	pr_err("invalid dram address %pa\n", &phyaddr);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(mvebu_mbus_get_dram_win_info);

int mvebu_mbus_get_io_win_info(phys_addr_t phyaddr, u32 *size, u8 *target,
			       u8 *attr)
{
	int win;

	for (win = 0; win < mbus_state.soc->num_wins; win++) {
		u64 wbase;
		int enabled;

		mvebu_mbus_read_window(&mbus_state, win, &enabled, &wbase,
				       size, target, attr, NULL);

		if (!enabled)
			continue;

		if (wbase <= phyaddr && phyaddr <= wbase + *size)
			return win;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(mvebu_mbus_get_io_win_info);

static __init int mvebu_mbus_debugfs_init(void)
{
	struct mvebu_mbus_state *s = &mbus_state;

	/*
	 * If no base has been initialized, doesn't make sense to
	 * register the debugfs entries. We may be on a multiplatform
	 * kernel that isn't running a Marvell EBU SoC.
	 */
	if (!s->mbuswins_base)
		return 0;

	s->debugfs_root = debugfs_create_dir("mvebu-mbus", NULL);
	if (s->debugfs_root) {
		s->debugfs_sdram = debugfs_create_file("sdram", S_IRUGO,
						       s->debugfs_root, NULL,
						       &mvebu_sdram_debug_fops);
		s->debugfs_devs = debugfs_create_file("devices", S_IRUGO,
						      s->debugfs_root, NULL,
						      &mvebu_devs_debug_fops);
	}

	return 0;
}
fs_initcall(mvebu_mbus_debugfs_init);

static int mvebu_mbus_suspend(void)
{
	struct mvebu_mbus_state *s = &mbus_state;
	int win;

	if (!s->mbusbridge_base)
		return -ENODEV;

	for (win = 0; win < s->soc->num_wins; win++) {
		void __iomem *addr = s->mbuswins_base +
			s->soc->win_cfg_offset(win);
		void __iomem *addr_rmp;

		s->wins[win].base = readl(addr + WIN_BASE_OFF);
		s->wins[win].ctrl = readl(addr + WIN_CTRL_OFF);

		if (!mvebu_mbus_window_is_remappable(s, win))
			continue;

		addr_rmp = s->mbuswins_base +
			s->soc->win_remap_offset(win);

		s->wins[win].remap_lo = readl(addr_rmp + WIN_REMAP_LO_OFF);
		s->wins[win].remap_hi = readl(addr_rmp + WIN_REMAP_HI_OFF);
	}

	s->mbus_bridge_ctrl = readl(s->mbusbridge_base +
				    MBUS_BRIDGE_CTRL_OFF);
	s->mbus_bridge_base = readl(s->mbusbridge_base +
				    MBUS_BRIDGE_BASE_OFF);

	return 0;
}

static void mvebu_mbus_resume(void)
{
	struct mvebu_mbus_state *s = &mbus_state;
	int win;

	writel(s->mbus_bridge_ctrl,
	       s->mbusbridge_base + MBUS_BRIDGE_CTRL_OFF);
	writel(s->mbus_bridge_base,
	       s->mbusbridge_base + MBUS_BRIDGE_BASE_OFF);

	for (win = 0; win < s->soc->num_wins; win++) {
		void __iomem *addr = s->mbuswins_base +
			s->soc->win_cfg_offset(win);
		void __iomem *addr_rmp;

		writel(s->wins[win].base, addr + WIN_BASE_OFF);
		writel(s->wins[win].ctrl, addr + WIN_CTRL_OFF);

		if (!mvebu_mbus_window_is_remappable(s, win))
			continue;

		addr_rmp = s->mbuswins_base +
			s->soc->win_remap_offset(win);

		writel(s->wins[win].remap_lo, addr_rmp + WIN_REMAP_LO_OFF);
		writel(s->wins[win].remap_hi, addr_rmp + WIN_REMAP_HI_OFF);
	}
}

static struct syscore_ops mvebu_mbus_syscore_ops = {
	.suspend	= mvebu_mbus_suspend,
	.resume		= mvebu_mbus_resume,
};

static int __init mvebu_mbus_common_init(struct mvebu_mbus_state *mbus,
					 phys_addr_t mbuswins_phys_base,
					 size_t mbuswins_size,
					 phys_addr_t sdramwins_phys_base,
					 size_t sdramwins_size,
					 phys_addr_t mbusbridge_phys_base,
					 size_t mbusbridge_size,
					 bool is_coherent)
{
	int win;

	mbus->mbuswins_base = ioremap(mbuswins_phys_base, mbuswins_size);
	if (!mbus->mbuswins_base)
		return -ENOMEM;

	mbus->sdramwins_base = ioremap(sdramwins_phys_base, sdramwins_size);
	if (!mbus->sdramwins_base) {
		iounmap(mbus_state.mbuswins_base);
		return -ENOMEM;
	}

	mbus->sdramwins_phys_base = sdramwins_phys_base;

	if (mbusbridge_phys_base) {
		mbus->mbusbridge_base = ioremap(mbusbridge_phys_base,
						mbusbridge_size);
		if (!mbus->mbusbridge_base) {
			iounmap(mbus->sdramwins_base);
			iounmap(mbus->mbuswins_base);
			return -ENOMEM;
		}
	} else
		mbus->mbusbridge_base = NULL;

	for (win = 0; win < mbus->soc->num_wins; win++)
		mvebu_mbus_disable_window(mbus, win);

	mbus->soc->setup_cpu_target(mbus);
	mvebu_mbus_setup_cpu_target_nooverlap(mbus);

	if (is_coherent)
		writel(UNIT_SYNC_BARRIER_ALL,
		       mbus->mbuswins_base + UNIT_SYNC_BARRIER_OFF);

	register_syscore_ops(&mvebu_mbus_syscore_ops);

	return 0;
}

int __init mvebu_mbus_init(const char *soc, phys_addr_t mbuswins_phys_base,
			   size_t mbuswins_size,
			   phys_addr_t sdramwins_phys_base,
			   size_t sdramwins_size)
{
	const struct of_device_id *of_id;

	for (of_id = of_mvebu_mbus_ids; of_id->compatible[0]; of_id++)
		if (!strcmp(of_id->compatible, soc))
			break;

	if (!of_id->compatible[0]) {
		pr_err("could not find a matching SoC family\n");
		return -ENODEV;
	}

	mbus_state.soc = of_id->data;

	return mvebu_mbus_common_init(&mbus_state,
			mbuswins_phys_base,
			mbuswins_size,
			sdramwins_phys_base,
			sdramwins_size, 0, 0, false);
}

#ifdef CONFIG_OF
/*
 * The window IDs in the ranges DT property have the following format:
 *  - bits 28 to 31: MBus custom field
 *  - bits 24 to 27: window target ID
 *  - bits 16 to 23: window attribute ID
 *  - bits  0 to 15: unused
 */
#define CUSTOM(id) (((id) & 0xF0000000) >> 24)
#define TARGET(id) (((id) & 0x0F000000) >> 24)
#define ATTR(id)   (((id) & 0x00FF0000) >> 16)

static int __init mbus_dt_setup_win(struct mvebu_mbus_state *mbus,
				    u32 base, u32 size,
				    u8 target, u8 attr)
{
	if (!mvebu_mbus_window_conflicts(mbus, base, size, target, attr)) {
		pr_err("cannot add window '%04x:%04x', conflicts with another window\n",
		       target, attr);
		return -EBUSY;
	}

	if (mvebu_mbus_alloc_window(mbus, base, size, MVEBU_MBUS_NO_REMAP,
				    target, attr)) {
		pr_err("cannot add window '%04x:%04x', too many windows\n",
		       target, attr);
		return -ENOMEM;
	}
	return 0;
}

static int __init
mbus_parse_ranges(struct device_node *node,
		  int *addr_cells, int *c_addr_cells, int *c_size_cells,
		  int *cell_count, const __be32 **ranges_start,
		  const __be32 **ranges_end)
{
	const __be32 *prop;
	int ranges_len, tuple_len;

	/* Allow a node with no 'ranges' property */
	*ranges_start = of_get_property(node, "ranges", &ranges_len);
	if (*ranges_start == NULL) {
		*addr_cells = *c_addr_cells = *c_size_cells = *cell_count = 0;
		*ranges_start = *ranges_end = NULL;
		return 0;
	}
	*ranges_end = *ranges_start + ranges_len / sizeof(__be32);

	*addr_cells = of_n_addr_cells(node);

	prop = of_get_property(node, "#address-cells", NULL);
	*c_addr_cells = be32_to_cpup(prop);

	prop = of_get_property(node, "#size-cells", NULL);
	*c_size_cells = be32_to_cpup(prop);

	*cell_count = *addr_cells + *c_addr_cells + *c_size_cells;
	tuple_len = (*cell_count) * sizeof(__be32);

	if (ranges_len % tuple_len) {
		pr_warn("malformed ranges entry '%pOFn'\n", node);
		return -EINVAL;
	}
	return 0;
}

static int __init mbus_dt_setup(struct mvebu_mbus_state *mbus,
				struct device_node *np)
{
	int addr_cells, c_addr_cells, c_size_cells;
	int i, ret, cell_count;
	const __be32 *r, *ranges_start, *ranges_end;

	ret = mbus_parse_ranges(np, &addr_cells, &c_addr_cells,
				&c_size_cells, &cell_count,
				&ranges_start, &ranges_end);
	if (ret < 0)
		return ret;

	for (i = 0, r = ranges_start; r < ranges_end; r += cell_count, i++) {
		u32 windowid, base, size;
		u8 target, attr;

		/*
		 * An entry with a non-zero custom field do not
		 * correspond to a static window, so skip it.
		 */
		windowid = of_read_number(r, 1);
		if (CUSTOM(windowid))
			continue;

		target = TARGET(windowid);
		attr = ATTR(windowid);

		base = of_read_number(r + c_addr_cells, addr_cells);
		size = of_read_number(r + c_addr_cells + addr_cells,
				      c_size_cells);
		ret = mbus_dt_setup_win(mbus, base, size, target, attr);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void __init mvebu_mbus_get_pcie_resources(struct device_node *np,
						 struct resource *mem,
						 struct resource *io)
{
	u32 reg[2];
	int ret;

	/*
	 * These are optional, so we make sure that resource_size(x) will
	 * return 0.
	 */
	memset(mem, 0, sizeof(struct resource));
	mem->end = -1;
	memset(io, 0, sizeof(struct resource));
	io->end = -1;

	ret = of_property_read_u32_array(np, "pcie-mem-aperture", reg, ARRAY_SIZE(reg));
	if (!ret) {
		mem->start = reg[0];
		mem->end = mem->start + reg[1] - 1;
		mem->flags = IORESOURCE_MEM;
	}

	ret = of_property_read_u32_array(np, "pcie-io-aperture", reg, ARRAY_SIZE(reg));
	if (!ret) {
		io->start = reg[0];
		io->end = io->start + reg[1] - 1;
		io->flags = IORESOURCE_IO;
	}
}

int __init mvebu_mbus_dt_init(bool is_coherent)
{
	struct resource mbuswins_res, sdramwins_res, mbusbridge_res;
	struct device_node *np, *controller;
	const struct of_device_id *of_id;
	const __be32 *prop;
	int ret;

	np = of_find_matching_node_and_match(NULL, of_mvebu_mbus_ids, &of_id);
	if (!np) {
		pr_err("could not find a matching SoC family\n");
		return -ENODEV;
	}

	mbus_state.soc = of_id->data;

	prop = of_get_property(np, "controller", NULL);
	if (!prop) {
		pr_err("required 'controller' property missing\n");
		return -EINVAL;
	}

	controller = of_find_node_by_phandle(be32_to_cpup(prop));
	if (!controller) {
		pr_err("could not find an 'mbus-controller' node\n");
		return -ENODEV;
	}

	if (of_address_to_resource(controller, 0, &mbuswins_res)) {
		pr_err("cannot get MBUS register address\n");
		return -EINVAL;
	}

	if (of_address_to_resource(controller, 1, &sdramwins_res)) {
		pr_err("cannot get SDRAM register address\n");
		return -EINVAL;
	}

	/*
	 * Set the resource to 0 so that it can be left unmapped by
	 * mvebu_mbus_common_init() if the DT doesn't carry the
	 * necessary information. This is needed to preserve backward
	 * compatibility.
	 */
	memset(&mbusbridge_res, 0, sizeof(mbusbridge_res));

	if (mbus_state.soc->has_mbus_bridge) {
		if (of_address_to_resource(controller, 2, &mbusbridge_res))
			pr_warn(FW_WARN "deprecated mbus-mvebu Device Tree, suspend/resume will not work\n");
	}

	mbus_state.hw_io_coherency = is_coherent;

	/* Get optional pcie-{mem,io}-aperture properties */
	mvebu_mbus_get_pcie_resources(np, &mbus_state.pcie_mem_aperture,
					  &mbus_state.pcie_io_aperture);

	ret = mvebu_mbus_common_init(&mbus_state,
				     mbuswins_res.start,
				     resource_size(&mbuswins_res),
				     sdramwins_res.start,
				     resource_size(&sdramwins_res),
				     mbusbridge_res.start,
				     resource_size(&mbusbridge_res),
				     is_coherent);
	if (ret)
		return ret;

	/* Setup statically declared windows in the DT */
	return mbus_dt_setup(&mbus_state, np);
}
#endif
