/* -*- linux-c -*-
 *
 *	$Id: sysrq.c,v 1.15 1998/08/23 14:56:41 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	based on ideas by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>
 *
 *	(c) 2000 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 *	overhauled to use key registration
 *	based upon discusions in irc://irc.openprojects.net/#kernelnewbies
 */

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/tty.h>
#include <linux/mount.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/kbd_kern.h>
#include <linux/proc_fs.h>
#include <linux/nmi.h>
#include <linux/quotaops.h>
#include <linux/perf_event.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>		/* for fsync_bdev() */
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/vt_kern.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/oom.h>
#include <linux/version.h>

#include <asm/ptrace.h>
#include <asm/irq_regs.h>

#include <linux/io.h>
#include <mach/gpio.h>
#include <mach/rk29_iomap.h>
#include <mach/iomux.h>

#include <asm/gpio.h>

#define GPIO_SWPORTA_DR   0x00
#define GPIO_SWPORTA_DDR  0x04

#define GPIO_SWPORTB_DR   0x0c
#define GPIO_SWPORTB_DDR  0x10

#define GPIO_SWPORTC_DR   0x18
#define GPIO_SWPORTC_DDR  0x1c

#define GPIO_SWPORTD_DR   0x24
#define GPIO_SWPORTD_DDR  0x28

#define RK_SYSRQ_GPIO_RES(name,resn0,resn1,oft0,oft1,oft2,oft3)			\
	{								\
		.res_name = name,				\
		.resmap[0] = {							\
			.id = 0,						\
			.resname = resn0,				\
		},									\
		.resmap[1] = {							\
			.id = 1,						\
			.resname = resn1,				\
		},									\
		.res_off = {oft0,oft1,oft2,oft3,0xffff},	\
	}

#define RK_SYSRQ_GPIO(name,base)			\
	{								\
		.rk_sysrq_gpio_name = name,				\
		.regbase = (const unsigned char __iomem *) base,	\
		.res_table = rk_sysrq_gpio_printres_table,\
		.res_table_count = ARRAY_SIZE(rk_sysrq_gpio_printres_table),	\
	}

#define RK_SYSRQ_IOMUX_RES(offt,start,mask,desc0,desc1,desc2,desc3)			\
	{								\
		.off = offt,				\
		.reg_description[0] = desc0,						\
		.reg_description[1] = desc1,						\
		.reg_description[2] = desc2,						\
		.reg_description[3] = desc3,						\
		.start_bit = start,\
		.mask_bit = mask,	\
	}
#define RK_SYSRQ_IOMUX_CFG(name,msg,regbase,table)						\
	{											\
		.reg_name = name,							\
		.message = msg,									\
		.reg_base = (const unsigned char __iomem *) regbase,				\
		.regres_table = rk_sysrq_iomux_res_gpio##table,		\
		.res_table_count = ARRAY_SIZE(rk_sysrq_iomux_res_gpio##table),\
	}
struct res_map
{
	const int id;
	const char *resname;
};
struct rk_sysrq_gpio_printres
{
	const char *res_name;
	struct res_map resmap[2];
	unsigned int res_off[5];
	
};
struct rk_sysrq_gpio
{
	const char *rk_sysrq_gpio_name;
	const unsigned char  __iomem *regbase;
	struct rk_sysrq_gpio_printres *res_table;
	unsigned int res_table_count;
};
struct rk_sysrq_iomux
{
	const char *reg_name;
	const char *message;
	const unsigned char  __iomem *reg_base;
	struct rk_sysrq_iomux_regres *regres_table;
	unsigned int res_table_count;
};
struct rk_sysrq_iomux_regres
{
	const unsigned short off;
	const char *reg_description[4];
	const unsigned char start_bit;
	const unsigned short mask_bit;
	
};

struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio0l[] = {
	RK_SYSRQ_IOMUX_RES(0x48,30,3,"GPIO0_B[7]","ebc_gdoe","smc_oe_n",NULL),
	RK_SYSRQ_IOMUX_RES(0x48,28,3,"GPIO0_B[6]","ebc_sdshr","smc_bls_n_1","host_int"),
	RK_SYSRQ_IOMUX_RES(0x48,26,3,"GPIO0_B[5]","ebc_vcom","smc_bls_n_0",NULL),
	RK_SYSRQ_IOMUX_RES(0x48,24,3,"GPIO0_B[4]","ebc_border1","smc_we_n",NULL),
	RK_SYSRQ_IOMUX_RES(0x48,22,3,"GPIO0_B[3]","ebc_border0","smc_addr[3]","host_data[3]"),
	RK_SYSRQ_IOMUX_RES(0x48,20,3,"GPIO0_B[2]","ebc_sdce2","smc_addr[2]","host_data[2]"),
	RK_SYSRQ_IOMUX_RES(0x48,18,3,"GPIO0_B[1]","ebc_sdce1","smc_addr[1]","host_data[1]"),
	RK_SYSRQ_IOMUX_RES(0x48,16,3,"GPIO0_B[0]","ebc_sdce0","smc_addr[0]","host_data[0]"),
	
	RK_SYSRQ_IOMUX_RES(0x48,14,3,"GPIO0_A[7]","mii_mdclk",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x48,12,3,"GPIO0_A[6]","mii_md",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x48,10,3,"GPIO0_A[5]","flash_dqs",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio0h[] = {
	RK_SYSRQ_IOMUX_RES(0x4c,30,3,"GPIO0_D[7]","flash_csn6",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,28,3,"GPIO0_D[6]","flash_csn5",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,26,3,"GPIO0_D[5]","flash_csn4",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,24,3,"GPIO0_D[4]","flash_csn3",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,22,3,"GPIO0_D[3]","flash_csn2",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,20,3,"GPIO0_D[2]","flash_csn1",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,18,3,"GPIO0_D[1]","ebc_gdclk","smc_addr[4]","host_data[4]"),
	RK_SYSRQ_IOMUX_RES(0x4c,16,3,"GPIO0_D[0]","ebc_sdoe","smc_adv_n",""),

	RK_SYSRQ_IOMUX_RES(0x4c,14,3,"GPIO0_C[7]","ebc_sdce5","smc_data15",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,12,3,"GPIO0_C[6]","ebc_sdce4","smc_data14",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,10,3,"GPIO0_C[5]","ebc_sdce3","smc_data13",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,8,3,"GPIO0_C[4]","ebc_gdpwr2","smc_data12",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,6,3,"GPIO0_C[3]","ebc_gdpwr1","smc_data11",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,4,3,"GPIO0_C[2]","ebc_gdpwr0","smc_data10",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,2,3,"GPIO0_C[1]","ebc_gdrl","smc_data9",NULL),
	RK_SYSRQ_IOMUX_RES(0x4c,0,3,"GPIO0_C[0]","ebc_gdsp","smc_data8",NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio1l[] = {
	RK_SYSRQ_IOMUX_RES(0x50,30,3,"GPIO1_B[7]","uart0_sout",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,28,3,"GPIO1_B[6]","uart0_sin",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,26,3,"GPIO1_B[5]","pwm0",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,24,3,"GPIO1_B[4]","vip_clkout",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,22,3,"GPIO1_B[3]","vip_data[3]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,20,3,"GPIO1_B[2]","vip_data[2]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,18,3,"GPIO1_B[1]","vip_data[1]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,16,3,"GPIO1_B[0]","vip_data[0]",NULL,NULL),
	
	RK_SYSRQ_IOMUX_RES(0x50,14,3,"GPIO1_A[7]","i2c1_scl",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,12,3,"GPIO1_A[6]","i2c1_sda",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,10,3,"GPIO1_A[5]","emmc_pwr_en","pwm3",NULL),
	RK_SYSRQ_IOMUX_RES(0x50,8,3,"GPIO1_A[4]","emmc_write_prt","spi0_csn1",NULL),
	RK_SYSRQ_IOMUX_RES(0x50,6,3,"GPIO1_A[3]","emmc_detect_n","spi1_csn1",NULL),
	RK_SYSRQ_IOMUX_RES(0x50,4,3,"GPIO1_A[2]","smc_csn1",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,2,3,"GPIO1_A[1]","smc_csn0",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x50,0,3,"GPIO1_A[0]","flash_cs7","mddr_tq",NULL),

};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio1h[] = {
	RK_SYSRQ_IOMUX_RES(0x54,30,3,"GPIO1_D[7]","sdmmc0_data[5]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,28,3,"GPIO1_D[6]","sdmmc0_data[4]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,26,3,"GPIO1_D[5]","sdmmc0_data[3]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,24,3,"GPIO1_D[4]","sdmmc0_data[2]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,22,3,"GPIO1_D[3]","sdmmc0_data[1]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,20,3,"GPIO1_D[2]","sdmmc0_data[0]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,18,3,"GPIO1_D[1]","sdmmc0_cmd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,16,3,"GPIO1_D[0]","sdmmc0_clkout",NULL,NULL),
	
	RK_SYSRQ_IOMUX_RES(0x54,14,3,"GPIO1_C[7]","sdmmc1_clkout",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,12,3,"GPIO1_C[6]","sdmmc1_data[3]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,10,3,"GPIO1_C[5]","sdmmc1_data[2]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,8,3,"GPIO1_C[4]","sdmmc1_data[1]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,6,3,"GPIO1_C[3]","sdmmc1_data[0]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,4,3,"GPIO1_C[2]","sdmmc1_cmd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x54,2,3,"GPIO1_C[1]","uart0_rts_n","sdmmc1_write_prt",NULL),
	RK_SYSRQ_IOMUX_RES(0x54,0,3,"GPIO1_C[0]","uart0_cts_n","sdmmc1_detect_n",NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio2l[] = {
	RK_SYSRQ_IOMUX_RES(0x58,30,3,"GPIO2_B[7]","i2c0_scl",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,28,3,"GPIO2_B[6]","i2c0_sda",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,26,3,"GPIO2_B[5]","uart3_rts_n","i2c3_scl",NULL),
	RK_SYSRQ_IOMUX_RES(0x58,24,3,"GPIO2_B[4]","uart3_cts_n","i2c3_sda",NULL),
	RK_SYSRQ_IOMUX_RES(0x58,22,3,"GPIO2_B[3]","uart3_sout",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,20,3,"GPIO2_B[2]","uart3_sin",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,18,3,"GPIO2_B[1]","uart2_sout",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,16,3,"GPIO2_B[0]","uart2_sin",NULL,NULL),

	RK_SYSRQ_IOMUX_RES(0x58,14,3,"GPIO2_A[7]","uart2_rts_n",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,12,3,"GPIO2_A[6]","uart2_cts_n",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,10,3,"GPIO2_A[5]","uart1_sout",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,8,3,"GPIO2_A[4]","uart1_sin",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,6,3,"GPIO2_A[3]","sdmmc0_write_prt","pwm2","uart1_sir_out_n"),
	RK_SYSRQ_IOMUX_RES(0x58,4,3,"GPIO2_A[2]","sdmmc0_detect_n",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,2,3,"GPIO2_A[1]","sdmmc0_data[7]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x58,0,3,"GPIO2_A[0]","sdmmc0_data[6]",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio2h[] = {
	RK_SYSRQ_IOMUX_RES(0x5c,30,3,"GPIO2_D[7]","i2s0_sdo3","mii_txd[3]",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,28,3,"GPIO2_D[6]","i2s0_sdo2","mii_txd[2]",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,26,3,"GPIO2_D[5]","i2s0_sdo1","mii_rxd[3]",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,24,3,"GPIO2_D[4]","i2s0_sdo0","mii_rxd[2]",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,22,3,"GPIO2_D[3]","i2s0_sdi","mii_col",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,20,3,"GPIO2_D[2]","i2s0_lrck_rx","mii_tx_err",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,18,3,"GPIO2_D[1]","i2s0_sclk","mii_crs",NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,16,3,"GPIO2_D[0]","i2s0_clk","mii_rx_clkin",NULL),
	
	RK_SYSRQ_IOMUX_RES(0x5c,14,3,"GPIO2_C[7]","spi1_rxd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,12,3,"GPIO2_C[6]","spi1_txd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,10,3,"GPIO2_C[5]","spi1_csn0",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,8,3,"GPIO2_C[4]","spi1_clk",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,6,3,"GPIO2_C[3]","spi0_rxd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,4,3,"GPIO2_C[2]","spi0_txd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,2,3,"GPIO2_C[1]","spi0_csn0",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x5c,0,3,"GPIO2_C[0]","spi0_clk",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio3l[] = {
	RK_SYSRQ_IOMUX_RES(0x60,30,3,"GPIO3_B[7]","emmc_data[5]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,28,3,"GPIO3_B[6]","emmc_data[4]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,26,3,"GPIO3_B[5]","emmc_data[3]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,24,3,"GPIO3_B[4]","emmc_data[2]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,22,3,"GPIO3_B[3]","emmc_data[1]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,20,3,"GPIO3_B[2]","emmc_data[0]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,18,3,"GPIO3_B[1]","emmc_cmd",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,16,3,"GPIO3_B[0]","emmc_clkout",NULL,NULL),

	RK_SYSRQ_IOMUX_RES(0x60,14,3,"GPIO3_A[7]","smc_addr[15]","host_data[15]",NULL),
	RK_SYSRQ_IOMUX_RES(0x60,12,3,"GPIO3_A[6]","smc_addr[14]","host_data[14]",NULL),
	RK_SYSRQ_IOMUX_RES(0x60,10,3,"GPIO3_A[5]","i2s1_lrck_tx",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,8,3,"GPIO3_A[4]","i2s1_sdo",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,6,3,"GPIO3_A[3]","i2s1_sdi",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,4,3,"GPIO3_A[2]","i2s1_lrck_rx",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,2,3,"GPIO3_A[1]","i2s1_sclk",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x60,0,3,"GPIO3_A[0]","i2s1_clk",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio3h[] = {
	RK_SYSRQ_IOMUX_RES(0x64,30,3,"GPIO3_D[7]","smc_addr[9]","host_data[9]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,28,3,"GPIO3_D[6]","smc_addr[8]","host_data[8]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,26,3,"GPIO3_D[5]","smc_addr[7]","host_data[7]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,24,3,"GPIO3_D[4]","host_wrn",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x64,22,3,"GPIO3_D[3]","host_rdn",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x64,20,3,"GPIO3_D[2]","host_csn",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x64,18,3,"GPIO3_D[1]","smc_addr[19]","host_addr[1]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,16,3,"GPIO3_D[0]","smc_addr[18]","host_addr[0]",NULL),

	RK_SYSRQ_IOMUX_RES(0x64,14,3,"GPIO3_C[7]","smc_addr[17]","host_data[17]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,12,3,"GPIO3_C[6]","smc_addr[16]","host_data[16]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,10,3,"GPIO3_C[5]","smc_addr[12]","host_data[12]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,8,3,"GPIO3_C[4]","smc_addr[11]","host_data[11]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,6,3,"GPIO3_C[3]","smc_addr[10]","host_data[10]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,4,3,"GPIO3_C[2]","smc_addr[13]","host_data[13]",NULL),
	RK_SYSRQ_IOMUX_RES(0x64,2,3,"GPIO3_C[1]","emmc_data[7]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x64,0,3,"GPIO3_C[0]","emmc_data[6]",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio4l[] = {
	RK_SYSRQ_IOMUX_RES(0X68,30,3,"GPIO4_B[7]","flash_data[15]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,28,3,"GPIO4_B[6]","flash_data[14]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,26,3,"GPIO4_B[5]","flash_data[13]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,24,3,"GPIO4_B[4]","flash_data[12]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,22,3,"GPIO4_B[3]","flash_data[11]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,20,3,"GPIO4_B[2]","flash_data[10]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,18,3,"GPIO4_B[1]","flash_data[9]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,16,3,"GPIO4_B[0]","flash_data[8]",NULL,NULL),

	RK_SYSRQ_IOMUX_RES(0X68,14,3,"GPIO4_A[7]","spdif_tx",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,12,3,"GPIO4_A[6]","otg1_drv_vbus",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X68,10,3,"GPIO4_A[5]","otg0_drv_vbus",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio4h[] = {
	RK_SYSRQ_IOMUX_RES(0X6c,30,3,"GPIO4_D[7]","i2s0_lrck_tx1",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,28,3,"GPIO4_D[6]","i2s0_lrck_tx0",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,26,3,"GPIO4_D[5]","trace_ctl",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,24,3,"GPIO4_D[4]","cpu trace_clk",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,22,3,"GPIO6_C[7:6]","cpu trace_data[7:6]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,20,3,"GPIO6_C[5:4]","cpu trace_data[5:4]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,18,3,"GPIO4_D[3:2]","cpu trace_data[3:2]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,16,3,"GPIO4_D[1:0]","cpu trace_data[1:0]",NULL,NULL),

	RK_SYSRQ_IOMUX_RES(0X6c,14,3,"GPIO4_C[7]","rmii_rxd[0]","mii_rxd[0]",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,12,3,"GPIO4_C[6]","rmii_rxd[1]","mii_rxd[1]",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,10,3,"GPIO4_C[5]","rmii_csr_dvalid","mii_rxd_valid",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,8,3,"GPIO4_C[4]","rmii_rx_err","mii_rx_err",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,6,3,"GPIO4_C[3]","rmii_txd[0]","mii_txd[0]",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,4,3,"GPIO4_C[2]","rmii_txd[1]","mii_txd[1]",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,2,3,"GPIO4_C[1]","rmii_tx_en","mii_tx_en",NULL),
	RK_SYSRQ_IOMUX_RES(0X6c,0,3,"GPIO4_C[0]","rmii_clkout","rmii_clkin",NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio5l[] = {
	RK_SYSRQ_IOMUX_RES(0x70,30,3,"GPIO5_B[7]","hsadc_clkout ","gps_clk ",NULL),
	RK_SYSRQ_IOMUX_RES(0x70,28,3,"GPIO5_B[6]","hsadc_data[9]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,26,3,"GPIO5_B[5]","hsadc_data[8]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,24,3,"GPIO5_B[4]","hsadc_data[7]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,22,3,"GPIO5_B[3]","hsadc_data[6]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,20,3,"GPIO5_B[2]","hsadc_data[5]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,18,3,"GPIO5_B[1]","hsadc_data[4]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,16,3,"GPIO5_B[0]","hsadc_data[3]",NULL,NULL),

	RK_SYSRQ_IOMUX_RES(0x70,14,3,"GPIO5_A[7]","hsadc_data[2]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,12,3,"GPIO5_A[6]","hsadc_data[1]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,10,3,"GPIO5_A[5]","hsadc_data[0]",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,8,3,"GPIO5_A[4]","ts_sync",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x70,6,3,"GPIO5_A[3]","mii_tx_clkin",NULL,NULL),
};
struct rk_sysrq_iomux_regres rk_sysrq_iomux_res_gpio5h[] = {
	RK_SYSRQ_IOMUX_RES(0x74,28,3,"GPIO5_D[6]","sdmmc1_pwr_en",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x74,26,3,"GPIO5_D[5]","sdmmc0_pwr_en",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x74,24,3,"GPIO5_D[4]","i2c2_scl",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x74,22,3,"GPIO5_D[3]","i2c2_sda",NULL,NULL),
	RK_SYSRQ_IOMUX_RES(0x74,20,3,"GPIO5_D[2]","pwm1","uart1_sir_in",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,18,3,"GPIO5_D[1]","ebc_sdclk","smc_addr[6]","host_data[6]"),
	RK_SYSRQ_IOMUX_RES(0x74,16,3,"GPIO5_D[0]","ebc_sdle","smc_addr[5]","host_data[5]"),
	
	RK_SYSRQ_IOMUX_RES(0x74,14,3,"GPIO5_C[7]","ebc_sddo7","smc_data7",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,12,3,"GPIO5_C[6]","ebc_sddo6","smc_data6",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,10,3,"GPIO5_C[5]","ebc_sddo5","smc_data5",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,8,3,"GPIO5_C[4]","ebc_sddo4","smc_data4",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,6,3,"GPIO5_C[3]","ebc_sddo3","smc_data3",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,4,3,"GPIO5_C[2]","ebc_sddo2","smc_data2",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,2,3,"GPIO5_C[1]","ebc_sddo1","smc_data1",NULL),
	RK_SYSRQ_IOMUX_RES(0x74,0,3,"GPIO5_C[0]","ebc_sddo0","smc_data0",NULL),
};
struct rk_sysrq_gpio_printres rk_sysrq_gpio_printres_table[] = {
	RK_SYSRQ_GPIO_RES("gpio pin data\0","L","H",GPIO_SWPORTA_DR,GPIO_SWPORTB_DR,GPIO_SWPORTC_DR,GPIO_SWPORTD_DR),
	RK_SYSRQ_GPIO_RES("gpio pin driction\0","O","I",GPIO_SWPORTA_DDR,GPIO_SWPORTB_DDR,GPIO_SWPORTB_DDR,GPIO_SWPORTB_DDR),
	RK_SYSRQ_GPIO_RES("gpio int enable\0","G","I",GPIO_INTEN,0xffff,0xffff,0xffff),
	RK_SYSRQ_GPIO_RES("gpio int MASK\0","I","M",GPIO_INTMASK,0xffff,0xffff,0xffff),
	RK_SYSRQ_GPIO_RES("gpio int type\0","L","E",GPIO_INTTYPE_LEVEL,0xffff,0xffff,0xffff),
	RK_SYSRQ_GPIO_RES("gpio int polarity\0","L","H",GPIO_INT_POLARITY,0xffff,0xffff,0xffff),
	RK_SYSRQ_GPIO_RES("gpio int status\0","N","I",GPIO_INT_STATUS,0xffff,0xffff,0xffff),
	
};
struct rk_sysrq_iomux rk_sysrq_iomux_table[] = {
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO0l_IOMUX",NULL,RK29_GRF_BASE,0l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO0h_IOMUX",NULL,RK29_GRF_BASE,0h),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO1l_IOMUX",NULL,RK29_GRF_BASE,1l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO1h_IOMUX",NULL,RK29_GRF_BASE,1h),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO2l_IOMUX",NULL,RK29_GRF_BASE,2l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO2h_IOMUX",NULL,RK29_GRF_BASE,2h),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO3l_IOMUX",NULL,RK29_GRF_BASE,3l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO3h_IOMUX",NULL,RK29_GRF_BASE,3h),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO4l_IOMUX",NULL,RK29_GRF_BASE,4l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO4h_IOMUX",NULL,RK29_GRF_BASE,4l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO5l_IOMUX",NULL,RK29_GRF_BASE,5l),
	RK_SYSRQ_IOMUX_CFG("GRF_GPIO5h_IOMUX",NULL,RK29_GRF_BASE,5h),
};
struct rk_sysrq_gpio rk_sysrq_gpio_table[] = {
		RK_SYSRQ_GPIO("GPIO 0",RK29_GPIO0_BASE),
		RK_SYSRQ_GPIO("GPIO 1",RK29_GPIO1_BASE),
		RK_SYSRQ_GPIO("GPIO 2",RK29_GPIO2_BASE),
		RK_SYSRQ_GPIO("GPIO 3",RK29_GPIO3_BASE),
		RK_SYSRQ_GPIO("GPIO 4",RK29_GPIO4_BASE),
		RK_SYSRQ_GPIO("GPIO 5",RK29_GPIO5_BASE),
		//RK_SYSRQ_GPIO("GPIO 0"),
		//RK_SYSRQ_GPIO("GPIO2",RK2818_GPIO2_BASE),
		//RK_SYSRQ_GPIO("GPIO3",RK2818_GPIO3_BASE),
};


static inline unsigned int rk_sysrq_reg_read(const unsigned char __iomem *regbase, unsigned int regOff)
{
	return __raw_readl(regbase + regOff);
}
char rk_sysrq_table_fistline_buf[1024];
#if 0
void rk_sysrq_draw_table(a,_)int a;int _;{

	char line_buf[129];
	int i,j;
	for(j = 0; j < 100; j++){
		if(j%3){
			for(i = 0; i < 100; i++){
				if(i%16)
					line_buf[i] = ' ';
				else
					line_buf[i] = '+';
			}
			line_buf[100] = 0;
			printk("%s\n",line_buf);
			}
			
		else{
			printk("%s\n",rk_sysrq_table_fistline_buf);
		}
		
	}
	printk("%s\n",rk_sysrq_table_fistline_buf);
}
#endif

/* Whether we react on sysrq keys or just ignore them */
int __read_mostly __rk_sysrq_enabled = 1;

static int __read_mostly rk_sysrq_always_enabled;

int rk_sysrq_on(void)
{
	return __rk_sysrq_enabled || rk_sysrq_always_enabled;
}

/*
 * A value of 1 means 'all', other nonzero values are an op mask:
 */
static inline int rk_sysrq_on_mask(int mask)
{
	return rk_sysrq_always_enabled || __rk_sysrq_enabled == 1 ||
						(__rk_sysrq_enabled & mask);
}

static int __init rk_sysrq_always_enabled_setup(char *str)
{
	rk_sysrq_always_enabled = 1;
	printk(KERN_INFO "debug: rk_sysrq always enabled.\n");

	return 1;
}

__setup("rk_sysrq_always_enabled", rk_sysrq_always_enabled_setup);


void rk_sysrq_get_gpio_status(void)
{
	
	return ;
}


static void rk_sysrq_gpio_show_reg(struct rk_sysrq_gpio *gpio_res)
{
	char rk_sysrq_line_buf[129];
	char *temp;
	unsigned int reg;
	int i = 0,j = 0, k = 0;
	rk_sysrq_line_buf[128] = 0xff;
	k = gpio_res->res_table_count;

    printk("-->%s ::\n",gpio_res->rk_sysrq_gpio_name);
    while(k--){
		for(j = 0; gpio_res->res_table[k].res_off[j] != 0xffff ; j++){
			temp = rk_sysrq_line_buf;
			memset(rk_sysrq_line_buf,0,128);
			printk("%s P[%c]: ",gpio_res->res_table[k].res_name,j+65);
			reg = rk_sysrq_reg_read(gpio_res->regbase, gpio_res->res_table[k].res_off[j]);
			for(i = 0; i < 8; i++){
				*temp++ = '[';
				*temp++ = (i+48);
				*temp++ = ']';
				*temp++ = ':';
				*temp++ = *(gpio_res->res_table[k].resmap[reg&1].resname);
				*temp++ = ' ';
				reg = reg>>1;
			}
			
			printk("%s\n",rk_sysrq_line_buf);
		}
		printk("\n");
	}
}
static void rk_sysrq_iomux_show_reg(struct rk_sysrq_iomux *mux)
{
	char *temp;
	const char *temp2;
	unsigned int reg;
	int i = 0,k = 0;
	if(mux->message)
		printk("%s\n",mux->message);
	reg = rk_sysrq_reg_read(mux->reg_base,mux->regres_table[0].off);
	for(i = 0 ; i < mux->res_table_count; i ++){
		unsigned int mask = mux->regres_table[i].mask_bit;
		unsigned int start_bit = mux->regres_table[i].start_bit;
		memset(rk_sysrq_table_fistline_buf,0,1024);
		temp = rk_sysrq_table_fistline_buf;
		for(k = 0 ; k < (mask + 1) ; k++){
			temp2 = mux->regres_table[i].reg_description[k];
			*temp++ = '[';*temp++ = (((k >> 1)&1) + '0');*temp++ = ((k&1) + '0');*temp++ = ']';*temp++ = ':';
			if(temp2 == NULL){
				*temp++ = 'r';*temp++ = 'e';*temp++ = 's';
			}else{
				while(*temp2)
					*temp++ = *temp2++;
			}
			*temp++ = ' ';
		}
		printk("%s\n",rk_sysrq_table_fistline_buf);
		memset(rk_sysrq_table_fistline_buf,0,1024);
		temp = rk_sysrq_table_fistline_buf;
		temp2 = mux->regres_table[i].reg_description[(reg >> start_bit)&mask];
		if(temp2 == NULL){
			*temp++ = 'r';*temp++ = 'e';*temp++ = 's';
		}else{
			while(*temp2)
				*temp++ = *temp2++;
		}
		printk("current set is : %s \n\n",rk_sysrq_table_fistline_buf);
	}
	return ;
}
static void rk_sysrq_handle_dump_gpio(int key, struct tty_struct *tty)
{
    int i,count = 0;
    count = ARRAY_SIZE(rk_sysrq_gpio_table);
    for(i = 0; i < count ; i++){
        rk_sysrq_gpio_show_reg(&rk_sysrq_gpio_table[i]);
    }
    count = ARRAY_SIZE(rk_sysrq_iomux_table);
    for(i = 0; i < count ; i++){
        rk_sysrq_iomux_show_reg(&rk_sysrq_iomux_table[i]);
    }

	return ;
}
static void rk_sysrq_handle_show_gpio(int key, struct tty_struct *tty)
{
    int i,count = 0;
    count = ARRAY_SIZE(rk_sysrq_gpio_table);
    for(i = 0; i < count ; i++){
        rk_sysrq_gpio_show_reg(&rk_sysrq_gpio_table[i]);
    }

	return ;
}
static void rk_sysrq_handle_show_iomux(int key, struct tty_struct *tty)
{
    int i,count = 0;

    count = ARRAY_SIZE(rk_sysrq_iomux_table);
    for(i = 0; i < count ; i++){
        rk_sysrq_iomux_show_reg(&rk_sysrq_iomux_table[i]);
    }

	return ;
}
static struct sysrq_key_op rk_sysrq_dump_gpio_op = {
	.handler	= rk_sysrq_handle_dump_gpio,
	.help_msg	= "dump gpio state(d)",
	.action_msg	= "Nice All RT Tasks",
	.enable_mask	= SYSRQ_ENABLE_RTNICE,
};
static struct sysrq_key_op rk_sysrq_show_gpio_op = {
	.handler	= rk_sysrq_handle_show_gpio,
	.help_msg	= "dump gpio state(o)",
	.action_msg	= "Nice All RT Tasks",
	.enable_mask	= SYSRQ_ENABLE_RTNICE,
};
static struct sysrq_key_op rk_sysrq_show_iomux_op = {
	.handler	= rk_sysrq_handle_show_iomux,
	.help_msg	= "dump gpio state(m)",
	.action_msg	= "Nice All RT Tasks",
	.enable_mask	= SYSRQ_ENABLE_RTNICE,
};
/* Key Operations table and lock */
static DEFINE_SPINLOCK(rk_sysrq_key_table_lock);

static struct sysrq_key_op *rk_sysrq_key_table[36] = {
	#if 0
	&sysrq_loglevel_op,		/* 0 */
	&sysrq_loglevel_op,		/* 1 */
	&sysrq_loglevel_op,		/* 2 */
	&sysrq_loglevel_op,		/* 3 */
	&sysrq_loglevel_op,		/* 4 */
	&sysrq_loglevel_op,		/* 5 */
	&sysrq_loglevel_op,		/* 6 */
	&sysrq_loglevel_op,		/* 7 */
	&sysrq_loglevel_op,		/* 8 */
	&sysrq_loglevel_op,		/* 9 */

	/*
	 * a: Don't use for system provided sysrqs, it is handled specially on
	 * sparc and will never arrive.
	 */
	NULL,				/* a */
	&sysrq_reboot_op,		/* b */
	&sysrq_crash_op,		/* c & ibm_emac driver debug */
	&sysrq_showlocks_op,		/* d */
	&sysrq_term_op,			/* e */
	&sysrq_moom_op,			/* f */
	/* g: May be registered for the kernel debugger */
	NULL,				/* g */
	NULL,				/* h - reserved for help */
	&sysrq_kill_op,			/* i */
#ifdef CONFIG_BLOCK
	&sysrq_thaw_op,			/* j */
#else
	NULL,				/* j */
#endif
	&sysrq_SAK_op,			/* k */
#ifdef CONFIG_SMP
	&sysrq_showallcpus_op,		/* l */
#else
	NULL,				/* l */
#endif
	&sysrq_showmem_op,		/* m */
	&sysrq_unrt_op,			/* n */
	/* o: This will often be registered as 'Off' at init time */
	NULL,				/* o */
	&sysrq_showregs_op,		/* p */
	&sysrq_show_timers_op,		/* q */
	&sysrq_unraw_op,		/* r */
	&sysrq_sync_op,			/* s */
	&sysrq_showstate_op,		/* t */
	&sysrq_mountro_op,		/* u */
	/* v: May be registered for frame buffer console restore */
	NULL,				/* v */
	&sysrq_showstate_blocked_op,	/* w */
	/* x: May be registered on ppc/powerpc for xmon */
	NULL,				/* x */
	/* y: May be registered on sparc64 for global register dump */
	NULL,				/* y */
	&sysrq_ftrace_dump_op,		/* z */
	#endif
	NULL,							/*0*/
	NULL,							/*1*/	
	NULL,							/*2*/
	NULL,							/*3*/
	NULL,							/*4*/
	NULL,							/*5*/
	NULL,							/*6*/
	NULL,							/*7*/
	NULL,							/*8*/
	NULL,							/*9*/
	NULL,							/*a*/
	NULL,							/*b*/
	NULL,							/*c*/
	&rk_sysrq_dump_gpio_op,			/*d*/
	NULL,							/*e*/
	NULL,							/*f*/
	NULL,							/*g*/
	NULL,							/*h*/
	NULL,							/*i*/
	NULL,							/*j*/
	NULL,							/*k*/
	NULL,							/*l*/
	&rk_sysrq_show_iomux_op,							/*m*/
	NULL,							/*n*/
	&rk_sysrq_show_gpio_op,							/*o*/
	NULL,							/*p*/
	NULL,							/*q*/
	NULL,							/*r*/
	NULL,							/*s*/
	NULL,							/*t*/
	NULL,							/*u*/
	NULL,							/*v*/
	NULL,							/*w*/
	NULL,							/*x*/
	NULL,							/*y*/
	NULL,							/*z*/

};

/* key2index calculation, -1 on invalid index */
static int rk_sysrq_key_table_key2index(int key)
{
	int retval;

	if ((key >= '0') && (key <= '9'))
		retval = key - '0';
	else if ((key >= 'a') && (key <= 'z'))
		retval = key + 10 - 'a';
	else
		retval = -1;
	return retval;
}

/*
 * get and put functions for the table, exposed to modules.
 */
struct sysrq_key_op *__rk_sysrq_get_key_op(int key)
{
        struct sysrq_key_op *op_p = NULL;
        int i;
	i = rk_sysrq_key_table_key2index(key);
	if (i != -1)
	        op_p = rk_sysrq_key_table[i];
        return op_p;
}

static void __rk_sysrq_put_key_op(int key, struct sysrq_key_op *op_p)
{
        int i = rk_sysrq_key_table_key2index(key);

        if (i != -1)
                rk_sysrq_key_table[i] = op_p;
}

/*
 * This is the non-locking version of handle_sysrq.  It must/can only be called
 * by sysrq key handlers, as they are inside of the lock
 */
void __rk_handle_sysrq(int key, struct tty_struct *tty, int check_mask)
{
	struct sysrq_key_op *op_p;
	int orig_log_level;
	int i;
	unsigned long flags;
	spin_lock_irqsave(&rk_sysrq_key_table_lock, flags);
	/*
	 * Raise the apparent loglevel to maximum so that the sysrq header
	 * is shown to provide the user with positive feedback.  We do not
	 * simply emit this at KERN_EMERG as that would change message
	 * routing in the consumers of /proc/kmsg.
	 */
	orig_log_level = console_loglevel;
	console_loglevel = 7;
	printk(KERN_INFO "rk_SysRq : ");

        op_p = __rk_sysrq_get_key_op(key);
        if (op_p) {
		/*
		 * Should we check for enabled operations (/proc/sysrq-trigger
		 * should not) and is the invoked operation enabled?
		 */
		if (!check_mask || rk_sysrq_on_mask(op_p->enable_mask)) {
			printk("%s\n", op_p->action_msg);
			console_loglevel = orig_log_level;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
			op_p->handler(key);
#else
			op_p->handler(key, tty);
#endif
		} else {
			printk("This sysrq operation is disabled.\n");
		}
	} else {
		printk("HELP : ");
		/* Only print the help msg once per handler */
		for (i = 0; i < ARRAY_SIZE(rk_sysrq_key_table); i++) {
			if (rk_sysrq_key_table[i]) {
				int j;

				for (j = 0; rk_sysrq_key_table[i] !=
						rk_sysrq_key_table[j]; j++)
					;
				if (j != i)
					continue;
				printk("%s ", rk_sysrq_key_table[i]->help_msg);
			}
		}
		printk("\n");
		console_loglevel = orig_log_level;
	}
	spin_unlock_irqrestore(&rk_sysrq_key_table_lock, flags);
}

/*
 * This function is called by the keyboard handler when SysRq is pressed
 * and any other keycode arrives.
 */
void rk_handle_sysrq(int key, struct tty_struct *tty)
{
	if (rk_sysrq_on())
		__rk_handle_sysrq(key, tty, 1);
}
EXPORT_SYMBOL(rk_handle_sysrq);

static int __rk_sysrq_swap_key_ops(int key, struct sysrq_key_op *insert_op_p,
                                struct sysrq_key_op *remove_op_p)
{

	int retval;
	unsigned long flags;
	spin_lock_irqsave(&rk_sysrq_key_table_lock, flags);
	if (__rk_sysrq_get_key_op(key) == remove_op_p) {
		__rk_sysrq_put_key_op(key, insert_op_p);
		retval = 0;
	} else {
		retval = -1;
	}
	spin_unlock_irqrestore(&rk_sysrq_key_table_lock, flags);
	return retval;
}

int rk_register_sysrq_key(int key, struct sysrq_key_op *op_p)
{
	return __rk_sysrq_swap_key_ops(key, op_p, NULL);
}
EXPORT_SYMBOL(rk_register_sysrq_key);

int rk_unregister_sysrq_key(int key, struct sysrq_key_op *op_p)
{
	return __rk_sysrq_swap_key_ops(key, NULL, op_p);
}
EXPORT_SYMBOL(rk_unregister_sysrq_key);

#ifdef CONFIG_PROC_FS
/*
 * writing 'C' to /proc/sysrq-trigger is like sysrq-C
 */
static ssize_t rk_write_sysrq_trigger(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	if (count) {
		char c;
		if (get_user(c, buf))
			return -EFAULT;
		__rk_handle_sysrq(c, NULL, 0);
	}
	return count;
}

static const struct file_operations rk_proc_sysrq_trigger_operations = {
	.write		= rk_write_sysrq_trigger,
};

static int __init rk_sysrq_init(void)
{
	memset(rk_sysrq_table_fistline_buf, '+', 128);
	rk_sysrq_table_fistline_buf[100] = 0;
	proc_create("rk-sysrq-trigger", S_IWUSR, NULL, &rk_proc_sysrq_trigger_operations);
	return 0;
}
module_init(rk_sysrq_init);
#endif
