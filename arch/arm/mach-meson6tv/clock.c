/*
 * arch/arm/mach-meson6tv/clock.c
 *
 * Copyright (C) 2011-2012 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

///#define DEBUG
///#define CONFIG_CPU_FREQ_DEBUG		1

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/cpu.h>

#include <linux/clkdev.h>
#include <plat/io.h>
#include <mach/clock.h>
#include <mach/hardware.h>
#include <mach/clk_set.h>
#include <mach/power_gate.h>
#include <plat/regops.h>
#include <plat/cpufreq.h>
#include <linux/printk.h>
#ifdef CONFIG_AMLOGIC_USB
#include <mach/usbclock.h>
#endif
#include <mach/am_regs.h>


static DEFINE_SPINLOCK(mali_clk_lock);
static DEFINE_SPINLOCK(clockfw_lock);
static DEFINE_MUTEX(clock_ops_lock);

static unsigned int mali_max = 333000;
static unsigned int freq_limit = 1;

static int set_sys_pll(struct clk *clk, unsigned long dst);
static void get_a9_divid(unsigned int idx, unsigned int * scale_divn, unsigned int * scale_out);
#define IS_CLK_ERR(a)  (IS_ERR(a) || a == 0)

#if 0
#ifdef CONFIG_INIT_A9_CLOCK_FREQ
static unsigned long __initdata init_clock = CONFIG_INIT_A9_CLOCK;
#else
static unsigned long __initdata init_clock = 0;
#endif
#endif

#if 0
static unsigned int gpu_to_min_cpu(unsigned int gpu);
#endif

static int _clk_set_rate_gpu(struct clk *clk, unsigned long gpu, unsigned long cpu);
static unsigned long clk_get_rate_a9(struct clk * clkdev);

#define CONFIG_MALI_MINIMUM_FREQ	0

// -----------------------------------------
// clk_util_clk_msr
// -----------------------------------------
// from twister_core.v
/**        .clk_to_msr_in          ( { 13'h0,                      // [63:50]
                                    cts_mipi_phy_clk,
                                    am_ring_osc_out_mali[1],    // [49]
                                    am_ring_osc_out_mali[0],    // [48]
                                    am_ring_osc_out_a9[1],      // [47]
                                    am_ring_osc_out_a9[0],      // [46]
                                    cts_pwm_A_clk,              // [45]
                                    cts_pwm_B_clk,              // [44]
                                    cts_pwm_C_clk,              // [43]
                                    cts_pwm_D_clk,              // [42]
                                    cts_eth_tx_clk,             // [41]
                                    cts_pcm_mclk,               // [40]
                                    cts_pcm_sclk,               // [39]
                                    cts_vdin_meas_clk,          // [38]
                                    cts_vdac_clk[1],            // [37]
                                    cts_hdmi_tx_pixel_clk,      // [36]
                                    cts_mali_clk,               // [35]
                                    cts_sdhc_clk1,              // [34]
                                    cts_sdhc_clk0,              // [33]
                                    cts_vdec_clk,               // [32]
                                    1'b0,                       // [31]
                                    cts_slow_ddr_clk,           // [30]
                                    cts_vdac_clk[0],            // [29]
                                    cts_sar_adc_clk,            // [28]
                                    cts_enci_clk,               // [27]
                                    sc_clk_int,                 // [26]
                                    sys_pll_div3,               // [25]
                                    lvds_fifo_clk,              // [24]
                                    HDMI_CH0_TMDSCLK,           // [23]
                                    clk_rmii_from_pad,            // [22]
                                    mod_audin_aoclk_i,          // [21]
                                    rtc_osc_clk_out,            // [20]
                                    cts_hdmi_sys_clk,           // [19]
                                    cts_led_pll_clk,            // [18]
                                    cts_vghl_pll_clk,           // [17]
                                    cts_FEC_CLK_2,              // [16]
                                    cts_FEC_CLK_1,              // [15]
                                    cts_FEC_CLK_0,              // [14]
                                    cts_amclk,                  // [13]
                                    vid2_pll_clk,               // [12]
                                    cts_eth_rmii,               // [11]
                                    cts_enct_clk,               // [10]
                                    cts_encl_clk,               // [9]
                                    cts_encp_clk,               // [8]
                                    clk81,                      // [7]
                                    vid_pll_clk,                // [6]
                                    usb1_clk_12mhz,             // [5]
                                    usb0_clk_12mhz,             // [4]
                                    ddr_pll_clk,                // [3]
                                    1'b0,                       // [2]
                                    am_ring_osc_clk_out_ee[1],     // [1]
                                    am_ring_osc_clk_out_ee[0]} ),  // [0]
*/
//
// For Example
//
// unsigend long    clk81_clk   = clk_util_clk_msr( 2,      // mux select 2
//                                                  50 );   // measure for 50uS
//
// returns a value in "clk81_clk" in Hz
//
// The "uS_gate_time" can be anything between 1uS and 65535 uS, but the limitation is
// the circuit will only count 65536 clocks.  Therefore the uS_gate_time is limited by
//
//   uS_gate_time <= 65535/(expect clock frequency in MHz)
//
// For example, if the expected frequency is 400Mhz, then the uS_gate_time should
// be less than 163.
//
// Your measurement resolution is:
//
//    100% / (uS_gate_time * measure_val )
//
//
// #define MSR_CLK_DUTY                               0x21d6
// #define MSR_CLK_REG0                               0x21d7
// #define MSR_CLK_REG1                               0x21d8
// #define MSR_CLK_REG2                               0x21d9
//
/**
unsigned long    clk_util_clk_msr(   unsigned long   clk_mux, unsigned long   uS_gate_time )
{
    // Set the measurement gate to 100uS
    Wr(MSR_CLK_REG0, (Rd(MSR_CLK_REG0) & ~(0xFFFF << 0)) | ((uS_gate_time-1) << 0) );
    // Disable continuous measurement
    // disable interrupts
    Wr(MSR_CLK_REG0, (Rd(MSR_CLK_REG0) & ~((1 << 18) | (1 << 17))) );
    Wr(MSR_CLK_REG0, (Rd(MSR_CLK_REG0) & ~(0x1f << 20)) | ((clk_mux << 20) |  // Select MUX
                                                          (1 << 19) |     // enable the clock
                                                          (1 << 16)) );    // enable measuring
    // Delay
    Rd(MSR_CLK_REG0);
    // Wait for the measurement to be done
    while( (Rd(MSR_CLK_REG0) & (1 << 31)) ) {}
    // disable measuring
    Wr(MSR_CLK_REG0, (Rd(MSR_CLK_REG0) & ~(1 << 16)) | (0 << 16) );

    unsigned long   measured_val = (Rd(MSR_CLK_REG2) & 0x000FFFFF); // only 20 bits

    if( measured_val == 0xFFFFF ) {     // 20 bits max
        return(0);
    } else {
        // Return value in Hz
        return(measured_val*(1000000/uS_gate_time));
    }
}
**/

#ifndef CONFIG_CLK_MSR_NEW
unsigned int clk_util_clk_msr(unsigned int clk_mux)
{
	unsigned int  msr;
	unsigned int regval = 0;
	aml_write_reg32(P_MSR_CLK_REG0,0);
    // Set the measurement gate to 64uS
	clrsetbits_le32(P_MSR_CLK_REG0,0xffff,64-1);
    // Disable continuous measurement
    // disable interrupts
    clrbits_le32(P_MSR_CLK_REG0,((1 << 18) | (1 << 17)));
	clrsetbits_le32(P_MSR_CLK_REG0,(0x1f<<20),(clk_mux<<20)|(1<<19)|(1<<16));

	aml_read_reg32(P_MSR_CLK_REG0);
    // Wait for the measurement to be done
      do {
        regval = aml_read_reg32(P_MSR_CLK_REG0);
    } while (regval & (1 << 31));
    // disable measuring
	clrbits_le32(P_MSR_CLK_REG0,(1 << 16));
	 msr=(aml_read_reg32(P_MSR_CLK_REG2)+31)&0x000FFFFF;
    // Return value in MHz*measured_val
    return (msr>>6)*1000000;



/*
    unsigned int regval = 0;
    /// Set the measurement gate to 64uS
    clrsetbits_le32(P_MSR_CLK_REG0,0xffff,63);

    // Disable continuous measurement
    // disable interrupts
    clrsetbits_le32(P_MSR_CLK_REG0,
        ((1 << 18) | (1 << 17)|(0x1f << 20)),///clrbits
        (clk_mux << 20) |                    /// Select MUX
        (1 << 19) |                          /// enable the clock
        (1 << 16));
    // Wait for the measurement to be done
    regval = aml_read_reg32(P_MSR_CLK_REG0);
    do {
        regval = aml_read_reg32(P_MSR_CLK_REG0);
    } while (regval & (1 << 31));

    // disable measuring
    clrbits_le32(P_MSR_CLK_REG0, (1 << 16));
    regval = (aml_read_reg32(P_MSR_CLK_REG2) + 31) & 0x000FFFFF;
    // Return value in MHz*measured_val
    return (regval >> 6)*1000000;
*/
}
#else
unsigned int clk_util_clk_msr(unsigned int clk_mux)
{
    unsigned int regval = 0;
    /// Set the measurement gate to 64uS
    clrsetbits_le32(P_MSR_CLK_REG0,0xffff,121);///122us

    // Disable continuous measurement
    // disable interrupts
    clrsetbits_le32(P_MSR_CLK_REG0,
        ((1 << 18) | (1 << 17)|(0x1f << 20)),///clrbits
        (clk_mux << 20) |                    /// Select MUX
        (1 << 19) |                          /// enable the clock
        (1 << 16));
    // Wait for the measurement to be done
    regval = aml_read_reg32(P_MSR_CLK_REG0);
    do {
        regval = aml_read_reg32(P_MSR_CLK_REG0);
    } while (regval & (1 << 31));

    // disable measuring
    clrbits_le32(P_MSR_CLK_REG0, (1 << 16));
    regval = (aml_read_reg32(P_MSR_CLK_REG2)) & 0x000FFFFF;
    regval += (regval/10000) * 6;
    // Return value in MHz*measured_val
    return (regval << 13);
}

#endif


#ifdef  CONFIG_CORE_FREQ_TRACK

#define CORE_FREQ_MAJOR     229
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/types.h>

#define CORE_FREQ_LINK_SIZE     1023

struct core_clock {
    unsigned long long  kernel_time;
    unsigned long       core_freq;
};

struct core_clock_info  {
    struct core_clock_info *next;
    struct core_clock       core_clock[CORE_FREQ_LINK_SIZE];
};

#define CPU_FREQ_MAX            1320
#define CPU_FREQ_STEP_SIZE      24
static struct core_clock core_clock_total[CPU_FREQ_MAX / CPU_FREQ_STEP_SIZE + 1];
static signed int  core_freq_track_start  = -1;
static unsigned int  core_freq_track_detail = 0;
static unsigned int  core_freq_track_cnt    = 0;
static unsigned int  core_freq_track_idx    = 0;
static unsigned long long start_time = 0;
static unsigned long long stop_time  = 0;
static unsigned long long prev_time  = 0;
static unsigned long prev_freq  = 0;
static struct core_clock_info *aml_core_clock_info = NULL;

static int core_freq_track_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int core_freq_track_release(struct inode *inode, struct file *file)
{
    return 0;
}

extern u64 sched_clock_cpu(int cpu);
static unsigned long long cpu_clock(int cpu)
{
	unsigned long long clock;
	unsigned long flags;

	local_irq_save(flags);
	clock = sched_clock_cpu(cpu);
	local_irq_restore(flags);

	return clock;
}

static void do_core_freq_track(unsigned long freq)
{
    unsigned long long ker_time;
    struct core_clock_info *next;
    struct core_clock_info *tmp;

    ker_time = cpu_clock(smp_processor_id());
    if (!core_freq_track_cnt) {                     // first record
        start_time = ker_time;
        prev_freq  = freq;
        prev_time  = ker_time;
        if (core_freq_track_detail && aml_core_clock_info) {
            aml_core_clock_info->core_clock[core_freq_track_idx].kernel_time = ker_time;
            aml_core_clock_info->core_clock[core_freq_track_idx].core_freq   = freq;
            core_freq_track_idx++;
        }
        core_freq_track_cnt++;
        return ;
    } else {
        core_clock_total[prev_freq / (CPU_FREQ_STEP_SIZE * 1000000)].kernel_time += ker_time - prev_time;
        prev_freq = freq;
        prev_time = ker_time;
        stop_time = ker_time;
        core_freq_track_cnt++;
        if (core_freq_track_detail && aml_core_clock_info) {
            aml_core_clock_info->core_clock[core_freq_track_idx].kernel_time = ker_time;
            aml_core_clock_info->core_clock[core_freq_track_idx].core_freq   = freq;
            core_freq_track_idx++;
            if (core_freq_track_idx >= CORE_FREQ_LINK_SIZE) {
                next = kzalloc(sizeof(*next), GFP_KERNEL);
                tmp  = aml_core_clock_info;
                while (tmp->next) {
                    tmp = tmp->next;
                }
                tmp->next = next;
                if (next == NULL) {
                    printk(" ##ERROR, allocate memory failed, stop track\n");
                    core_freq_track_start = 0;
                    return;
                }
                core_freq_track_idx = 0;
            }
        }
    }
}

static ssize_t core_freq_track_read(struct file *file, char __user *buf, size_t len,
                                    loff_t *ppos)
{
    int     size = 0;
    char    rbuf[200];
    unsigned long long t;
    unsigned long long total_time = stop_time - start_time;
    unsigned long long temp_time;
    unsigned long tmp1, tmp2;
    unsigned long percent;
    unsigned long ns;
    static read_step = 0;
    static read_idx  = 0;
    static struct core_clock_info *next = NULL;
    struct core_clock_info *tmp;

    if (core_freq_track_start < 0) {
        printk("usage:\n"
               "echo 0 > /dev/core_freq     stop track\n"
               "     1 > /dev/core_freq     start track, but not collect details\n"
               "     2 > /dev/core_freq     start track, with details\n");
        return 0;
    }
    // first , sprintf total statistics
    if (!read_step) {
        if (!read_idx) {
            size += sprintf(rbuf + size, "freq,      time,     %\n");
        }
        while (read_idx < ARRAY_SIZE(core_clock_total)) {
            if (!core_clock_total[read_idx].kernel_time) {
                read_idx++;
            } else {
                if (core_clock_total[read_idx].kernel_time) {
                    t  = core_clock_total[read_idx].kernel_time;
                    temp_time = t;
                    do_div(temp_time,  100000);
                    do_div(total_time, 100000000);
                    tmp1 = (unsigned long)temp_time;
                    tmp2 = (unsigned long)total_time;
                    percent = tmp1 / tmp2;
                    ns = do_div(t, 1000000000);
                    size += sprintf(rbuf + size, "%4d, %6u.%06u, %2d.%01d\n",
                                    read_idx * 24, (unsigned long)t, ns / 1000,
                                    percent / 10, percent % 10);
                }
                read_idx++;
                break;
            }
        }
        if (read_idx >= ARRAY_SIZE(core_clock_total)) {
            read_idx = 0;
            read_step++;
            size += sprintf(rbuf + size, "start time,   stop time,   total time\n");
            t  = start_time;
            ns = do_div(t, 1000000000);
            size += sprintf(rbuf + size, "%6u.%06u, ", (unsigned long)t, ns / 1000);
            t  = stop_time;
            ns = do_div(t, 1000000000);
            size += sprintf(rbuf + size, "%6u.%06u, ",  (unsigned long)t, ns / 1000);
            t  = stop_time - start_time;;
            ns = do_div(t, 1000000000);
            size += sprintf(rbuf + size, "%6u.%06u\n", (unsigned long)t, ns / 1000);
            if (core_freq_track_detail) {
                size += sprintf(rbuf + size, "\ndetails:\n");
                next = aml_core_clock_info;
            }
        }
        copy_to_user(buf, rbuf, size);
        return size;
    }
    if (read_step == 1) {       // sprintf details
        if (next) {
            if (next->core_clock[read_idx].core_freq) {
                t = next->core_clock[read_idx].kernel_time;
                ns = do_div(t, 1000000000);
                size += sprintf(rbuf + size, "%6d.%06d, %4d\n",
                                (unsigned long)t, ns / 1000, next->core_clock[read_idx].core_freq / 1000000);
            } else {
                // at end of detail
                kfree(next);
                read_step = 0;
                read_idx  = 0;
                next = NULL;
                aml_core_clock_info = NULL;
                return 0;
            }
            read_idx++;
            if (read_idx >= CORE_FREQ_LINK_SIZE) {
                tmp  = next;
                next = next->next;
                kfree(tmp);
            }
            copy_to_user(buf, rbuf, size);
            return size;
        }
    }

    read_step = 0;
    read_idx  = 0;
    next = NULL;

    return 0;
}

static ssize_t core_freq_track_write(struct file *file, char __user *buf, size_t len,
                                     loff_t *ppos)
{
    char    value;
    unsigned long long ker_time;
    struct core_clock_info *tmp, *next;

    copy_from_user(&value, buf, sizeof(char));

    switch (value) {
    case '0':
        if (core_freq_track_start) {
            ker_time = cpu_clock(0);
            core_clock_total[prev_freq / (CPU_FREQ_STEP_SIZE * 1000000)].kernel_time += ker_time - prev_time;
            stop_time = ker_time;
            core_freq_track_cnt++;
            if (core_freq_track_detail && aml_core_clock_info) {
                aml_core_clock_info->core_clock[core_freq_track_idx].kernel_time = ker_time;
                aml_core_clock_info->core_clock[core_freq_track_idx].core_freq   = prev_freq;
                core_freq_track_idx++;
                if (core_freq_track_idx >= CORE_FREQ_LINK_SIZE) {
                    next = kzalloc(sizeof(*next), GFP_KERNEL);
                    tmp  = aml_core_clock_info;
                    while (tmp->next) {
                        tmp = tmp->next;
                    }
                    tmp->next = next;
                    if (next == NULL) {
                        printk(" ##ERROR, allocate memory failed, stop track\n");
                        core_freq_track_start = 0;
                        return;
                    }
                    core_freq_track_idx = 0;
                }
            }
        }
        core_freq_track_start  = 0;
        printk("core freq track stop, got %d samples\n", core_freq_track_cnt);
        break;

    case '1':
        memset(core_clock_total, 0, sizeof(core_clock_total));
        core_freq_track_cnt    = 0;
        core_freq_track_detail = 0;
        core_freq_track_start  = 1;
        printk("start track, but not with details\n");
        break;

    case '2':
        memset(core_clock_total, 0, sizeof(core_clock_total));
        if (!aml_core_clock_info) {
            aml_core_clock_info = kzalloc(sizeof(struct core_clock_info), GFP_KERNEL);
            if (!aml_core_clock_info) {
                printk("ERROR, allocate memory failed\n");
                return -1;
            } else {
                printk(" allocate memory success, addr:%p\n", aml_core_clock_info);
            }
        }
        core_freq_track_cnt    = 0;
        core_freq_track_idx    = 0;
        core_freq_track_detail = 1;
        core_freq_track_start  = 1;
        printk("start track, with details\n");
        break;

    default:
        printk("unsupport command, %c\n", value);
        break;
    }
    return len;
}

static struct file_operations core_freq_ops = {
    .open    = core_freq_track_open,
    .read    = core_freq_track_read,
    .write   = core_freq_track_write,
    .release = core_freq_track_release,
};

static struct class *core_freq_class = NULL;

static int __init core_freq_track_init(void)
{
    int res;

    res = register_chrdev(CORE_FREQ_MAJOR, "core_freq", &core_freq_ops);
    core_freq_class = class_create(THIS_MODULE, "core_freq");
    if (res < 0 || !core_freq_class) {
        printk("register dev failed\n");
        return -1;
    }
    device_create(core_freq_class, NULL, MKDEV(CORE_FREQ_MAJOR, 2), NULL, "core_freq");
    return 0;
}

static void __exit core_freq_track_exit(void)
{
    unregister_chrdev(CORE_FREQ_MAJOR, "core_freq");
}

module_init(core_freq_track_init);
module_exit(core_freq_track_exit);

#endif  /* CONFIG_CORE_FREQ_TRACK */


unsigned long mali_clock_gating_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&mali_clk_lock, flags);

    return flags;
}
EXPORT_SYMBOL(mali_clock_gating_lock);

void mali_clock_gating_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&mali_clk_lock, flags);
}
EXPORT_SYMBOL(mali_clock_gating_unlock);


int    clk_measure(char  index )
{
	const char* clk_table[]={
	" CTS_MIPI_PHY_CLK(50)",
	" AM_RING_OSC_OUT_MALI[1](49)",
	" AM_RING_OSC_OUT_MALI[0](48)",
	" AM_RING_OSC_OUT_A9[1](47)",
	" AM_RING_OSC_OUT_A9[0](46)",
	" CTS_PWM_A_CLK(45)",
	" CTS_PWM_B_CLK(44)",
	" CTS_PWM_C_CLK(43)",
	" CTS_PWM_D_CLK(42)",
	" CTS_ETH_TX(41)",
	" CTS_PCM_MCLK(40)",
	" CTS_PCM_SCLK(39)",
	" CTS_VDIN_MEAS_CLK(38)",
	" CTS_VDAC_CLK1(37)",
	" CTS_HDMI_TX_PIXEL_CLK(36)",
	" CTS_MALI_CLK (35)",
	" CTS_SDHC_CLK1(34)",
	" CTS_SDHC_CLK0(33)",
	" CTS_VDAC_CLK(32)",
	" Reserved(31)",
	" CTS_SLOW_DDR_CLK(30)",
	" CTS_VDAC_CLK0(29)",
	" CTS_SAR_ADC_CLK(28)",
	" CTS_ENCI_CL(27)",
	" SC_CLK_INT(26)",
	" SYS_PLL_DIV3(25)",
	" LVDS_FIFO_CLK(24)",
	" HDMI_CH0_TMDSCLK(23)",
	" CLK_RMII_FROM_PAD (22)",
	" MOD_AUDIN_AMCLK_I(21)",
	" RTC_OSC_CLK_OUT (20)",
	" CTS_HDMI_SYS_CLK(19)",
	" CTS_LED_PLL_CLK(18)",
	" CTS_VGHL_PLL_CLK (17)",
	" CTS_FEC_CLK_2(16)",
	" CTS_FEC_CLK_1 (15)",
	" CTS_FEC_CLK_0 (14)",
	" CTS_AMCLK(13)",
	" VID2_PLL_CLK(12)",
	" CTS_ETH_RMII(11)",
	" CTS_ENCT_CLK(10)",
	" CTS_ENCL_CLK(9)",
	" CTS_ENCP_CLK(8)",
	" CLK81 (7)",
	" VID_PLL_CLK(6)",
	" USB1_CLK_12MHZ (5)",
	" USB0_CLK_12MHZ (4)",
	" DDR_PLL_CLK(3)",
	" Reserved(2)",
	" AM_RING_OSC_CLK_OUT1(1)",
	" AM_RING_OSC_CLK_OUT0(0)",
	};
	int  i;
	int len = sizeof(clk_table)/sizeof(char*) - 1;
	if (index  == 0xff)
	{
	 	for(i = 0;i < len;i++)
		{
			printk("[%10d]%s\n",clk_util_clk_msr(i),clk_table[len-i]);
		}
		return 0;
	}
	printk("[%10d]%s\n",clk_util_clk_msr(index),clk_table[len-index]);
	return 0;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

    if (rate < clk->min)
        return clk->min;

    if (rate > clk->max)
        return clk->max;

    return rate;
}
EXPORT_SYMBOL(clk_round_rate);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

    if (clk->get_rate)
		return clk->get_rate(clk);
	else
		return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

int on_parent_changed(struct clk *clk, int rate, int before,int failed)
{
	struct clk_ops* pops = clk->clk_ops;
	while(pops){
		if(before == 1){
				if(pops->clk_ratechange_before)
					pops->clk_ratechange_before(rate,pops->privdata);
		}
		else{
				if(pops->clk_ratechange_after)
					pops->clk_ratechange_after(rate,pops->privdata,failed);
		}
		pops = pops->next;
	}
	return 0;
}

int meson_notify_childs_changed(struct clk *clk,int before,int failed)
{
	struct clk* p;
	if(IS_CLK_ERR(clk))
		return 0;
	p = (struct clk*)(clk->child.next);
	if (p) {
		unsigned long flags;

		int rate = clk_get_rate(p);
		spin_lock_irqsave(&clockfw_lock, flags);
		on_parent_changed(p,rate,before,failed);
		spin_unlock_irqrestore(&clockfw_lock, flags);

		meson_notify_childs_changed(p,before,failed);

		p = (struct clk*)p->sibling.next;
		while(p){
		  spin_lock_irqsave(&clockfw_lock, flags);
			on_parent_changed(p,rate,before,failed);
			spin_unlock_irqrestore(&clockfw_lock, flags);

			meson_notify_childs_changed(p,before,failed);

			p = (struct clk*)p->sibling.next;
		}
	}
	return 0;
}

//flow. self -> child -> child slibling
int meson_clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags=0;
	int ret;
	int ops_run_count;
	struct clk_ops *p;

	if(clk->set_rate == NULL || IS_CLK_ERR(clk))
			return 0;
	//post message before clk change.
	{
			ret = 0;
			ops_run_count = 0;
			p = clk->clk_ops;
			while(p){
				ops_run_count++;
				if(p->clk_ratechange_before)
					ret = p->clk_ratechange_before(rate, p->privdata);
				if(ret != 0)
					break;
				p = p->next;
			}
			meson_notify_childs_changed(clk,1,ret);
	}


	if(ret == 0){
	  if (!clk->open_irq)
	      spin_lock_irqsave(&clockfw_lock, flags);
	  else
	      spin_lock(&clockfw_lock);
//		printk(KERN_INFO "%s() clk=%p rate=%lu\n", __FUNCTION__, clk, rate);
	  if(clk->set_rate)
	  	ret = clk->set_rate(clk, rate) ;
	  if (!clk->open_irq)
	      spin_unlock_irqrestore(&clockfw_lock, flags);
	  else
	      spin_unlock(&clockfw_lock);
	}

	//post message after clk change.
	{
			int idx = 0;
			p = clk->clk_ops;
			while(p){
				idx++;
				if(idx > ops_run_count)
					break;
				if(p->clk_ratechange_after)
						p->clk_ratechange_after(rate, p->privdata,ret);
				p = p->next;
			}
	}

	meson_notify_childs_changed(clk,0,ret);

  return ret;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret=0;
	int parent_rate = 0;
	if(IS_CLK_ERR(clk))
		return 0;
	if(clk_get_rate(clk) == rate){
			return 0;
	}

	if(clk->need_parent_changed){
		unsigned long flags;
	  spin_lock_irqsave(&clockfw_lock, flags);
		parent_rate = clk->need_parent_changed(clk, rate);
	  spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	if(parent_rate != 0)
		clk_set_rate(clk->parent,parent_rate);
	else{
		mutex_lock(&clock_ops_lock);
		//printk(KERN_INFO "%s() clk=%p rate=%lu\n", __FUNCTION__, clk, rate);
		ret = meson_clk_set_rate(clk,rate);
	 	mutex_unlock(&clock_ops_lock);
	}
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

unsigned long long clkparse(const char *ptr, char **retptr)
{
    char *endptr;   /* local pointer to end of parsed string */

    unsigned long long ret = simple_strtoull(ptr, &endptr, 0);

    switch (*endptr) {
    case 'G':
    case 'g':
        ret *= 1000;
    case 'M':
    case 'm':
        ret *= 1000;
    case 'K':
    case 'k':
        ret *= 1000;
        endptr++;
    default:
        break;
    }

    if (retptr) {
        *retptr = endptr;
    }

    return ret;
}

int meson_enable(struct clk *clk)
{
	if (IS_CLK_ERR(clk))
		return 0;

	if (clk_get_status(clk) == 1)
		return 0;

	if (meson_enable(clk->parent) == 0) {
			struct clk_ops *p;
			int idx;
			int ops_run_count = 0;
			int ret = 0;
			p = clk->clk_ops;
			while(p){
					ops_run_count++;
					if(p->clk_enable_before)
						ret = p->clk_enable_before(p->privdata);
					if(ret == 1)
						break;
					p = p->next;
			}

			if(ret == 0){
				if(clk->enable)
					ret = clk->enable(clk);
				else if(clk->clk_gate_reg_adr != 0)
					aml_set_reg32_mask(clk->clk_gate_reg_adr,clk->clk_gate_reg_mask);
					ret = 0;
			}

			p = clk->clk_ops;
			idx = 0;
			while(p){
				idx++;
				if(idx > ops_run_count)
					break;
				if(p->clk_enable_after)
					 p->clk_enable_after(p->privdata,ret);
				p = p->next;
			}

			return ret;
		}
		else
			return 1;
}
int clk_enable(struct clk *clk)
{
		int ret;
		mutex_lock(&clock_ops_lock);
		ret = meson_enable(clk);
		mutex_unlock(&clock_ops_lock);
		return ret;
}
EXPORT_SYMBOL(clk_enable);

int  meson_clk_disable(struct clk *clk)
{
		int ret = 0;
		int ops_run_count = 0;
		if(IS_CLK_ERR(clk))
			return 0;
		if(clk_get_status(clk) == 0)
			return 0;

		if(clk->child.next){
			struct clk* pchild = (struct clk*)(clk->child.next);
			if(meson_clk_disable(pchild) != 0)
				return 1;
			pchild = (struct clk*)pchild->sibling.next;
			while(pchild){
				if(meson_clk_disable(pchild) != 0)
					return 1;
				pchild = (struct clk*)pchild->sibling.next;
			}
		}

		//do clk disable
		//post message before clk disable.
		{
			struct clk_ops *p;
			ret = 0;
			p = clk->clk_ops;
			while(p){
				ops_run_count++;
				if(p->clk_disable_before)
					ret = p->clk_disable_before(p->privdata);
				if(ret != 0)
					break;
				p = p->next;
			}
		}

		//do clock gate disable
		if(ret == 0){
			if(clk->disable)
				ret = clk->disable(clk);
			else if(clk->clk_gate_reg_adr != 0){
					aml_clr_reg32_mask(clk->clk_gate_reg_adr,clk->clk_gate_reg_mask);
					ret = 0;
			}
		}

		//post message after clk disable.
		{
			struct clk_ops *p;
			int idx = 0;
			p = clk->clk_ops;
			while(p){
				idx++;
				if(idx > ops_run_count)
					break;
				if(p->clk_disable_after)
						p->clk_disable_after(p->privdata,ret);
				p = p->next;
			}
		}

		return ret;
}

void clk_disable(struct clk *clk)
{
		mutex_lock(&clock_ops_lock);
		meson_clk_disable(clk);
		mutex_unlock(&clock_ops_lock);
}
EXPORT_SYMBOL(clk_disable);

/**
 * Section all get rate functions
 */
static unsigned long clk_msr_get(struct clk * clk)
{
	uint32_t temp;
	uint32_t cnt = 0;
	if(clk->rate>0)
	{
		return clk->rate;
	}
	if(clk->msr>0)
	{
		clk->rate = clk_util_clk_msr(clk->msr);
	}else if (clk->parent){
		cnt=clk_get_rate(clk->parent);
		cnt /= 1000000;
		clk->msr_mul=clk->msr_mul?clk->msr_mul:1;
		clk->msr_div=clk->msr_div?clk->msr_div:1;
		temp=cnt*clk->msr_mul;
		clk->rate=temp/clk->msr_div;
		clk->rate *= 1000000;
	}
	return clk->rate;
}

static unsigned long clk_get_rate_xtal(struct clk * clkdev)
{
	unsigned long clk;
	clk = aml_get_reg32_bits(P_PREG_CTLREG0_ADDR, 4, 6);
	clk = clk * 1000 * 1000;
	return clk;
}

static unsigned long clk_get_rate_sys(struct clk * clkdev)
{
	unsigned long clk;
	if (clkdev && clkdev->rate)
		clk = clkdev->rate;
	else {
		//using measure sys div3 to get sys pll clock. (25)
		unsigned long mul, div, od, temp;
		unsigned long long result;
		clk = clk_get_rate_xtal(NULL);
		temp = aml_read_reg32(P_HHI_SYS_PLL_CNTL);
		mul=temp&((1<<9)-1);
		div=(temp>>9)&0x3f;
		od=(temp>>16)&3;
		result=((u64)clk)*((u64)mul);
		do_div(result,div);
		clk = (unsigned long)(result>>od);
	}
	return clk;
}

static unsigned long clk_get_rate_a9(struct clk * clkdev)
{
	unsigned long clk = 0;
	unsigned int sysclk_cntl;

	if (clkdev && clkdev->rate)
		return clkdev->rate;

	sysclk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
	if((sysclk_cntl & (1<<7)) == 0)
		clk = clk_get_rate_xtal(NULL);
	else{
		unsigned long parent_clk = 0;
		unsigned int pll_sel = sysclk_cntl&3;
		if(pll_sel == 0)
			parent_clk = clk_get_rate_xtal(NULL);
		else if(pll_sel == 1)
			parent_clk = clk_get_rate_sys(clkdev->parent);
		else if(pll_sel == 2)
			parent_clk = clk_util_clk_msr(3);//ddr clock
		else
			printk(KERN_INFO "Error : A9 parent pll selection incorrect!");
		if(parent_clk > 0){
			unsigned int N = (sysclk_cntl >> 8) & 0x3F;
			unsigned int div = 1;
			unsigned sel = (sysclk_cntl >> 2) & 3;
			if(sel == 1)
				div = 2;
			else if(sel == 2)
				div = 3;
			else if(sel == 3)
				div = 2 * N;
			clk = parent_clk / div;
		}
	}
	if (clk == 0) {
		pr_info("clk_get_rate_a9 measured clk=0 sysclk_cntl=%#x\n", sysclk_cntl);
	}

	return clk;
}

static unsigned long a9_cur_clk = 0;
#ifdef CONFIG_HAVE_ARM_TWD
static unsigned long clk_get_rate_smp_twd(struct clk * clkdev)
{

    return (a9_cur_clk ? a9_cur_clk : clk_get_rate_a9(NULL)) / 4;
}
#endif /* CONFIG_HAVE_ARM_TWD */

/**
 * udelay will delay depending on lpj.  lpj is adjusted before|after
 * cpu freq is changed, so udelay could take longer or shorter than
 * expected. This function scales the udelay value to get a more
 * accurate delay during cpu freq changes.
 * lpj is adjust elsewhere, so drivers don't need to worry about this.
 */
static inline void udelay_scaled(unsigned long usecs, unsigned int oldMHz,
                                 unsigned int newMHz)
{
	udelay(usecs * newMHz / oldMHz);
}

/**
 *  Internal CPU clock rate setting function.
 *
 *  MUST be called with proper protection.
 */
static int _clk_set_rate_cpu(struct clk *clk, unsigned long cpu, unsigned long gpu)
{
	unsigned long parent = 0;
	unsigned long oldcpu = clk_get_rate_a9(clk);
	unsigned int cpu_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
	unsigned int idx;

	//printk(KERN_INFO "(CTS_CPU_CLK) %ldMHz --> %ldMHz (0x%x)\n", clk_get_rate_a9(clk) / 1000000, cpu / 1000000, cpu_clk_cntl);

	/**
	 *  CPU <-> GPU clock need to satisfy the following:
	 *
	 *      CPU > GPU
	 *
	 *  If GPU is busy then abort the scaling request.
	 */
#if 0
	if (aml_read_reg32(P_HHI_MALI_CLK_CNTL) & (1 << 8)) {
		if (!gpu)
			gpu = clk_util_clk_msr(35);
		if (cpu <= gpu) {
			unsigned int min_cpu = gpu_to_min_cpu(gpu / 1000) * 1000;
			// already at min
			if (oldcpu == min_cpu)
				return -EINVAL;
			// bring target cpu freq down only to minimum this gpu freq supports
			if (cpu < min_cpu)
				cpu = min_cpu;
		}
		else if(cpu < gpu + 24000000)
    			cpu += 24000000;
	}
#endif
	if ((cpu_clk_cntl & 3) == 1) {
		unsigned int n = 0;
		//unsigned char factor = 1;
		unsigned int scale_out = 0;

		parent = clk_get_rate_sys(clk->parent);
		// CPU switch to xtal
		aml_write_reg32(P_HHI_SYS_CPU_CLK_CNTL, cpu_clk_cntl & ~(1 << 7));
		if (oldcpu <= cpu) {
			// when increasing frequency, lpj has already been adjusted
			udelay_scaled(10, cpu / 1000000, 24 /*clk_get_rate_xtal*/);
		} else {
			// when decreasing frequency, lpj has not yet been adjusted
			udelay_scaled(10, oldcpu / 1000000, 24 /*clk_get_rate_xtal*/);
		}
#if 0
		if (parent == cpu)
			scale_out = 0;
		else if ((parent >> 1) == cpu)
			scale_out = 1;
		else if (parent == ((cpu << 1) + cpu))
			scale_out = 2;
		else {
			parent = cpu;

			while (parent < 750000000) {
				switch (factor) {
				case 1:
					scale_out = 1;
					factor = 2;
					break;
				case 2:
					scale_out = 2;
					factor = 3;
					break;
				default:
					scale_out = 3;
					n++;
					factor = (n << 1);
					break;
				}

				parent = cpu * factor;
			}

			set_sys_pll(clk->parent, parent);
		}

		cpu_clk_cntl = ((cpu_clk_cntl & ~((3 << 2) | (0x3f << 8))) | (scale_out << 2) | (n << 8));
		printk(KERN_INFO "(CTS_CPU_CLK) syspll=%lu n=%d scale_out=%d cpu_clk_cntl=0x%x\n", parent, n, scale_out, cpu_clk_cntl);
		aml_write_reg32(P_HHI_SYS_CPU_CLK_CNTL, cpu_clk_cntl);
#else
	#if 0
		//set_sys_pll(clk->parent, cpu);
	#else
		clk->parent->old_rate = oldcpu;
		idx = set_sys_pll(clk->parent, cpu);
		get_a9_divid(idx, &n, &scale_out);
		parent = clk_get_rate_sys(clk->parent);
		// update actual cpu freq
		cpu = parent;
		cpu_clk_cntl = ((cpu_clk_cntl & ~((3 << 2) | (0x3f << 8))) | (scale_out << 2) | (n << 8));
#ifdef CONFIG_CPU_FREQ_DEBUG_DETAIL
		pr_debug("(CTS_CPU_CLK) syspll=%lu n=%d scale_out=%d cpu_clk_cntl=0x%x\n", parent, n, scale_out, cpu_clk_cntl);
#endif /* CONFIG_CPU_FREQ_DEBUG_DETAIL */
		aml_write_reg32(P_HHI_SYS_CPU_CLK_CNTL, cpu_clk_cntl);
	#endif
#endif
		//_clk_set_rate_gpu(clk_get_sys("mali", "pll_fixed"), gpu / 1000000, cpu);
		// cpu increased, adjust gpu
		#if 0
		if (cpu > a9_cur_clk)
			_clk_set_rate_gpu(NULL, 0, cpu);
		#endif
		// Read CBUS for short delay, then CPU switch to sys pll
		cpu_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
		aml_write_reg32(P_HHI_SYS_CPU_CLK_CNTL, (cpu_clk_cntl) | (1 << 7));
		if (oldcpu <= cpu) {
			// when increasing frequency, lpj has already been adjusted
			udelay(100);
		} else {
			// when decreasing frequency, lpj has not yet been adjusted
			udelay_scaled(100, oldcpu / 1000000, cpu / 1000000);
		}

		// CPU switch to sys pll
		//cpu_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
		//aml_set_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL, (1 << 7));
 	}

	clk->rate = cpu;

#ifdef CONFIG_CPU_FREQ_DEBUG
	pr_debug("(CTS_CPU_CLK) CPU %ld.%ldMHz\n", clk_get_rate_a9(clk) / 1000000, clk_get_rate_a9(clk)%1000000);
#endif /* CONFIG_CPU_FREQ_DEBUG */

#ifdef CONFIG_CORE_FREQ_TRACK
    if (core_freq_track_start > 0) {
        do_core_freq_track(clk_get_rate_a9(clk));
    }
#endif  /* CONFIG_CORE_FREQ_TRACK */

	return 0;
}

#ifdef CONFIG_SMP
#define USE_ON_EACH_CPU 0
struct clk_change_info{
  int cpu;
  struct clk * clk;
  unsigned long rate;
  int err;
};

#define MESON_CPU_CONTROL_REG (IO_AHB_BASE + 0x1ff80)
#define MESON_CPU1_CONTROL_ADDR_REG (IO_AHB_BASE + 0x1ff84)
#define MESON_CPU_STATUS_REG(cpu) (IO_AHB_BASE + 0x1ff90 +(cpu<<2))
#define MESON_CPU_STATUS(cpu) aml_read_reg32(MESON_CPU_STATUS_REG(cpu))
#define MESON_CPU_SET_STATUS(status) aml_write_reg32(MESON_CPU_STATUS_REG(smp_processor_id()),status)

#define MESON_CPU_SLEEP		1
#define MESON_CPU_WAKEUP	2

void meson_set_cpu_ctrl_reg(int value)
{
	spin_lock(&clockfw_lock);
	aml_write_reg32(MESON_CPU_CONTROL_REG, value);
	spin_unlock(&clockfw_lock);
}
#if 0
static unsigned long cpu_sleep_max_count = 0;
static unsigned long cpu_wait_max_count = 0;

static unsigned tag_print=0;
#endif
static inline unsigned long meson_smp_wait_others(unsigned status)
{
	unsigned long count = 0;
	int mask;
	int cpu = 0, my = smp_processor_id();

	mask = (((1 << nr_cpu_ids) - 1) & (~(1 << my)));
	do {
		__asm__ __volatile__ ("wfe" : : : "memory");
		for_each_online_cpu(cpu) {

			if (cpu != my && MESON_CPU_STATUS(cpu) == status) {
				count++;
				mask &= ~(1 << cpu);
			}
		}

	} while (mask);

	return count;
}

static inline void meson_smp_init_transaction(void)
{
    int cpu;

    aml_write_reg32(MESON_CPU_CONTROL_REG, 0);

    for_each_online_cpu(cpu) {
        aml_write_reg32(MESON_CPU_STATUS_REG(cpu), 0);
    }
}

static void smp_a9_clk_change(struct clk_change_info * info)
{
    int cpu = smp_processor_id();
#if USE_ON_EACH_CPU
    unsigned long count = 0;
    if (cpu != info->cpu) {
        unsigned long flags;
        MESON_CPU_SET_STATUS(MESON_CPU_SLEEP);
        pr_debug("CPU%u: Hey CPU %d, I am going to sleep\n", cpu, info->cpu);
        smp_wmb();
	dsb_sev();
	local_irq_save(flags);
        while ((aml_read_reg32(MESON_CPU_CONTROL_REG) & (1 << cpu)) == 0) {
		count++;

            __asm__ __volatile__ ("wfe" : : : "memory");
        }
        local_irq_restore(flags);
        MESON_CPU_SET_STATUS(MESON_CPU_WAKEUP);

        if (count > cpu_sleep_max_count) cpu_sleep_max_count = count;

        pr_debug("CPU%u: Hey CPU %d, I woke up (%lu %lu)\n", cpu, info->cpu, count, cpu_sleep_max_count);
        smp_wmb();
        dsb_sev();
    }
    else

    {
        pr_debug("CPU%u: Hey other CPU, I am waiting for you to sleep\n", cpu);

        count = meson_smp_wait_others(MESON_CPU_SLEEP);
        if (count > cpu_wait_max_count) cpu_wait_max_count = count;

        pr_debug("CPU%u: All other CPU in sleep (%lu %lu)\n", cpu, count, cpu_wait_max_count);

        info->err = _clk_set_rate_cpu(info->clk, info->rate, 0);
        aml_write_reg32(MESON_CPU_CONTROL_REG, 0xf);
        smp_wmb();
        dsb_sev();
    }

#else
        if(cpu!=info->cpu){
            info->err = _clk_set_rate_cpu(info->clk, info->rate, 0);
        }
#endif
}
#endif /* CONFIG_SMP */



static int clk_set_rate_a9(struct clk *clk, unsigned long rate)
{
#ifdef CONFIG_SMP
	struct clk_change_info info;
#endif /* CONFIG_SMP */
	int error = 0;

	if (rate < 1000)
		rate *= 1000000;

	if(freq_limit && rate > 1200000000)
	{
		rate = 1200000000;
		printk("cpu freq limited to %lu \n", rate);
	}
#ifdef CONFIG_SMP
#if USE_ON_EACH_CPU
	if (aml_read_reg32(MESON_CPU_CONTROL_REG)) {
#else
	if (num_online_cpus()>1) {
#endif
		info.cpu  = smp_processor_id();
		info.clk  = clk;
		info.rate = rate;
		info.err  = 0;
#if USE_ON_EACH_CPU
		meson_smp_init_transaction();
		on_each_cpu((void (*) (void * info))smp_a9_clk_change, &info, 0);
#else
		smp_call_function((void (*) (void * info))smp_a9_clk_change, &info, 1);
#endif
		error = info.err;
	}
	else {
		error = _clk_set_rate_cpu(clk, rate, 0);
	}

	if (error == 0)
		a9_cur_clk = clk->rate;
#else
	error = _clk_set_rate_cpu(clk, rate, 0);
#endif /* CONFIG_SMP */

	return error;
}

#ifdef CONFIG_CLK81_DFS
static int clk81_target_rate = 0;

static int set_clk81_clock(int rate)
{
    aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 0, 8, 1); //switch to xtal
    if (rate <= 100000000) {//100M
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 3, 0, 7); //div 4
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 7, 12, 3); //switch to fclk_div5
    } else if (rate > 100000000 && rate <= 118000000) {//111M
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 5, 0, 7); //div 6
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 6, 12, 3); //switch to fclk_div3
    } else if (rate > 118000000 && rate <= 132000000) {//125M
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 7, 0, 7); //div 8
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 5, 12, 3); //switch to fclk_div2
    } else if (rate > 132000000 && rate <= 180000000) {//167M
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 3, 0, 7); //div 8
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 6, 12, 3); //switch to fclk_div3
    } else if (rate > 180000000) {//200M
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 1, 0, 7); //div 2
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 7, 12, 3); //switch to fclk_div5
    } else if (rate > 200000000) {//222M
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 2, 0, 7); //div 3
        aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 6, 12, 3); //switch to fclk_div3
    }
    aml_read_reg32(P_HHI_MPEG_CLK_CNTL);
    aml_set_reg32_bits(P_HHI_MPEG_CLK_CNTL, 1, 8, 1);
}

int check_and_set_clk81(void)
{
	if (clk81_target_rate > 0) {
		set_clk81_clock(clk81_target_rate);
		clk81_target_rate = 0;
	}
	return 0;
}

static int cal_final_clk81_clk(int rate)
{
    int ret;
    if (rate <= 100000000) {//100M
        ret = 100000000;
    } else if (rate > 100000000 && rate <= 118000000) {//111M
        ret = 111000000;
    } else if (rate > 118000000 && rate <= 132000000) {//125M
        ret = 125000000;
    } else if (rate > 132000000 && rate <= 180000000) {//167M
        ret = 167000000;
    } else if (rate > 180000000) {//200M
        ret = 200000000;
    } else if (rate > 200000000) {//222M
        ret = 222000000;
    }
    return ret;
}

static int clk_set_rate_clk81(struct clk *clk, unsigned long rate)
{
    int clk81_rate;
    clk81_rate = clk_get_rate(clk);
    if (cal_final_clk81_clk(rate) == clk81_rate)
        return 0;
    printk("pre clk81 rate is %d\n", clk81_rate);
    printk("new clk81 rate is %d\n", rate);

    clk81_target_rate = rate;

    while(clk81_target_rate >0)
        msleep(2);

    clk->rate = clk_util_clk_msr(7); //mesure current clk81 clock

    clk81_rate = clk_get_rate(clk);
    aml_clr_reg32_mask(P_UART0_CONTROL, (1 << 19) | 0xFFF);
    aml_set_reg32_mask(P_UART0_CONTROL, (((clk81_rate / (115200 * 4)) - 1) & 0xfff));
    aml_clr_reg32_mask(P_UART1_CONTROL, (1 << 19) | 0xFFF);
    aml_set_reg32_mask(P_UART1_CONTROL, (((clk81_rate / (115200 * 4)) - 1) & 0xfff));
    aml_clr_reg32_mask(P_AO_UART_CONTROL, (1 << 19) | 0xFFF);
    aml_set_reg32_bits(P_AO_UART_CONTROL, ((clk81_rate / (115200 * 4)) - 1) & 0xfff, 0, 12);
    printk("                          \n");
    printk("clk81 switch to %d\n", clk81_rate);
}
#endif /* CONFIG_CLK81_DFS */

static unsigned long clk_get_rate_gpu(struct clk * clkdev)
{
	unsigned long clk = 0;
	unsigned int gpu_clk_cntl = aml_read_reg32(P_HHI_MALI_CLK_CNTL);
	int src = (gpu_clk_cntl >> 9) & 7;
	int N = (gpu_clk_cntl & 0x7F) + 1;

	//printk(KERN_INFO "%s() cntl=0x%x src=%d N=%d\n", __FUNCTION__, gpu_clk_cntl, src, N);
	if (src == 7)
		clk = 2000000000 / (5 * N);
	else if (src == 6)
		clk = 2000000000 / (3 * N);
	else if (src == 5)
		clk = 2000000000 / (2 * N);
	else
		clk = 0;

	return clk;
}

static unsigned int gpu_pll_cntl_lookup[] = {
	0x0400,	// DPLL / 1  = N/A (400)
	0x0400,	// DPLL / 2  = N/A (400)
	0x0400,	// DPLL / 3  = N/A (400)
	0x0400,	// DPLL / 4  = N/A (400)
	0x0400,	// DPLL / 5  = 400
	0x0201,	// DPLL / 6  = 333
	0x0003,	// DPLL / 7  = N/A (250)
	0x0003,	// DPLL / 8  = 250
	0x0202,	// DPLL / 9  = 222
	0x0401,	// DPLL / 10 = 200
	0x0203,	// DPLL / 11 = N/A (167)
	0x0203,	// DPLL / 12 = 167
	0x0006,	// DPLL / 13 = N/A (143)
	0x0006,	// DPLL / 14 = 143
	0x0204,	// DPLL / 15 = 133
	0x0007,	// DPLL / 16 = 125
	0x0205,	// DPLL / 17 = N/A (111)
	0x0205,	// DPLL / 18 = 111
	0x0403,	// DPLL / 19 = N/A (100)
	0x0403,	// DPLL / 20 = 100
	0x0206,	// DPLL / 21 = 95
	0x0207,	// DPLL / 22 = N/A (83)
	0x0207,	// DPLL / 23 = N/A (83)
	0x0207,	// DPLL / 24 = 83
	0x0209,	// DPLL / 25 = N/A (66)
	0x0209,	// DPLL / 26 = N/A (66)
	0x0209,	// DPLL / 27 = N/A (66)
	0x0209,	// DPLL / 28 = N/A (66)
	0x0209,	// DPLL / 29 = N/A (66)
	0x0209,	// DPLL / 30 = 66
};

#if 0
static unsigned int gpu_to_min_cpu(unsigned int gpu)
{
	if (gpu == 0)
		gpu = mali_max;
	//NOTICE cpu rates must be rounded to final rate
	if (gpu <=  83000) return  96000;
	if (gpu <= 111000) return 120000;
	if (gpu <= 167000) return 168000;
	if (gpu <= 250000) return 264000;
	if (gpu <= 333000) return 336000;
	return 408000;
}
#endif

/**
 *  Internal GPU clock rate setting function.
 *
 *  MUST be called with proper protection.
 */
static int _clk_set_rate_gpu(struct clk *clk, unsigned long gpu, unsigned long cpu)
{
	unsigned long mali_flags;
	int enabled;

	cpu /= 1000000;
#if 0
	if (!gpu) {
		if (cpu >= 400)
			gpu = 400;
		else if (cpu > 333)
			gpu = 333;
		else if (cpu > 250)
			gpu = 250;
		else if (cpu > 166)
			gpu = 166;
		else if (cpu > 111)
			gpu = 111;
		else if (cpu > 83)
			gpu = 83;
		else
			gpu = 66;
	} else if (gpu > 400)
		gpu = 400;

	if (gpu > (mali_max / 1000))
		gpu = mali_max / 1000;

	if (gpu == (clk_get_rate_gpu(NULL) / 1000000))
		return 0;
#endif
	mali_flags = mali_clock_gating_lock();

	enabled = (aml_read_reg32(P_HHI_MALI_CLK_CNTL) & (1 << 8));
	if (enabled)
		aml_clr_reg32_mask((P_HHI_MALI_CLK_CNTL), (1 << 8));

	aml_clr_reg32_mask(P_HHI_MALI_CLK_CNTL, 0x7F | (0x7 << 9));
	aml_set_reg32_mask(P_HHI_MALI_CLK_CNTL,0xf00);//0xd01=330M; 0xf00=400M

	mali_clock_gating_unlock(mali_flags);

	if (enabled) {
		pr_debug("%s() cpu=%luMHz gpu=%luMHz idx=%ld cntl=0x%x\n", __FUNCTION__, cpu, gpu, 2000 / gpu - 1, gpu_pll_cntl_lookup[2000 / gpu - 1]);
		pr_debug("%s() gpu=%luMHz\n", __FUNCTION__, clk_get_rate_gpu(NULL) / 1000000);
	}

	return 0;
}

static int clk_set_rate_mali(struct clk *clk, unsigned long rate)
{
	pr_debug("%s() GPU %luMHz --> %luMHz\n", __FUNCTION__, clk_get_rate_gpu(NULL) / 1000000, rate / 1000000);

	if (clk->priv)
		_clk_set_rate_gpu(clk, 0, clk_get_rate_a9(clk->priv));

	pr_debug("%s() %luMHz\n", __FUNCTION__, clk_get_rate_gpu(NULL) / 1000000);

	return 0;
}

static int clk_enable_mali(struct clk *clk)
{
#if 0
	/* Turn on mali clock */
	unsigned long cpu = 0;
	unsigned int min_cpu;
#endif
	unsigned long flags;
	unsigned long mali_flags;

	if (!clk->priv) {
		return -1;
	}
#if 0
#ifdef CONFIG_CPU_FREQ
	cpu = clk_get_rate_a9(clk->priv);
	min_cpu = gpu_to_min_cpu(mali_max);
	if ((cpu / 1000) < min_cpu) {
		/* bump CPU up to get Mali enabled at higher freq. Using cpufreq
		 * to do so, which will properly adjust voltage and jiffies
		 */
		meson_cpufreq_boost(min_cpu);
	}
#endif
#endif

	spin_lock_irqsave(&clockfw_lock, flags);

	//cpu = clk_get_rate_a9(clk->priv);
	_clk_set_rate_gpu(NULL, 0, 0);

    mali_flags = mali_clock_gating_lock();
	aml_set_reg32_mask(P_HHI_MALI_CLK_CNTL, (1 << 8));
    mali_clock_gating_unlock(mali_flags);

#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_INFO "%s() GPU=%luMHz CPU=%luMhz\n", __FUNCTION__, clk_get_rate_gpu(NULL) / 1000000, clk_get_rate_a9(clk->priv) / 1000000);
#endif /* CONFIG_CPU_FREQ_DEBUG */

	spin_unlock_irqrestore(&clockfw_lock, flags);
	return 0;
}

static int clk_disable_mali(struct clk *clk)
{
	/* Turn off mali clock */
	unsigned long flags;
	unsigned long mali_flags;

	spin_lock_irqsave(&clockfw_lock, flags);
#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_INFO "%s() GPU=%luMHz CPU=%luMhz\n", __FUNCTION__, clk_get_rate_gpu(NULL) / 1000000, clk_get_rate_a9(clk->priv) / 1000000);
#endif /* CONFIG_CPU_FREQ_DEBUG */

    mali_flags = mali_clock_gating_lock();
	aml_clr_reg32_mask(P_HHI_MALI_CLK_CNTL, (1 << 8));
    mali_clock_gating_unlock(mali_flags);

	spin_unlock_irqrestore(&clockfw_lock, flags);
	return 0;
}
#if 0
static int clk_status_mali(struct clk *clk)
{
	/* Check mali clock status */
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	return aml_read_reg32(P_HHI_MALI_CLK_CNTL) & (1 << 8)
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
#endif
static unsigned long clk_get_rate_vid2(struct clk * clkdev)
{
	unsigned long clk;
	unsigned int viid_cntl = aml_read_reg32(P_HHI_VIID_PLL_CNTL);
	unsigned long parent_clk;
	unsigned od,M,N;
	parent_clk = clk_get_rate(clkdev->parent);
	if(!viid_cntl)
		return 0;
	parent_clk /= 1000000;
	od = (viid_cntl>>16)&3;
	M = viid_cntl&0x1FF;
	N = (viid_cntl>>9)&0x1F;
	if(od == 0)
		od = 1;
	else if(od == 1)
		od = 2;
	else if(od == 2)
		od = 4;
	clk = parent_clk * M / N;
	clk /= od;
	clk *= 1000000;
	return clk;
}

static unsigned long clk_get_rate_hpll(struct clk * clkdev)
{
	unsigned long clk;
	unsigned int vid_cntl = aml_read_reg32(P_HHI_VID_PLL_CNTL);
	unsigned long parent_clk;
	unsigned od_fb,od_hdmi,od_ldvs,M,N;
	parent_clk = clk_get_rate(clkdev->parent);
	parent_clk /= 1000000;
	od_ldvs = (vid_cntl>>16)&3;
	od_hdmi = (vid_cntl>>18)&3;
	od_fb = (vid_cntl>>20)&3;
	M = vid_cntl&0x3FF;
	N = (vid_cntl>>10)&0x1F;
	if(od_hdmi == 0)
		od_hdmi = 1;
	else if(od_hdmi == 1)
		od_hdmi = 2;
	else if(od_hdmi == 2)
		od_hdmi = 4;
	if(od_fb == 0)
		od_fb = 1;
	else if(od_fb == 1)
		od_fb = 2;
	else if(od_fb == 2)
		od_fb = 4;

	clk = parent_clk * M * od_fb / N;
	clk /= od_hdmi;
	clk *= 1000000;
	return clk;
}

#define CLK_DEFINE(devid,conid,msr_id,setrate,getrate,en,dis,privdata)  \
    static struct clk clk_##devid={                                     \
        .set_rate=setrate,.get_rate=getrate,.enable=en,.disable=dis,    \
        .priv=privdata,.parent=&clk_##conid ,.msr=msr_id                \
    };                                                                  \
    static struct clk_lookup clk_lookup_##devid={                       \
        .dev_id=#devid,.con_id=#conid,.clk=&clk_##devid                 \
    };clkdev_add(&clk_lookup_##devid)

///TOP level
static struct clk clk_xtal = {
	.rate		= -1,
	.get_rate	= clk_get_rate_xtal,
};

static struct clk_lookup clk_lookup_xtal = {
	.dev_id		= "xtal",
	.con_id		= NULL,
	.clk		= &clk_xtal
};

#if 1
#define SYS_PLL_TABLE_MIN	  48000000
#define SYS_PLL_TABLE_MAX	1512000000

struct sys_pll_s {
    unsigned cntl;
    unsigned cntl2;
    unsigned cntl3;
    unsigned cntl4;
    unsigned scan;
    unsigned scale_divn;
};

static unsigned sys_pll_settings[][6] = {
	{0x20220, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 3, 2},  // 48 = 192 / 2 / 2
	{0x20224, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 2},  // 72 = 216 / 3
	{0x20220, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},  // 96
	{0x20228, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},  // 120
	{0x20230, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},  // 144
	{0x20238, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},  // 168
	{0x20220, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 192
	{0x20224, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 216
	{0x20228, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 240
	{0x2022C, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 264
	{0x20230, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 288
	{0x20234, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 312
	{0x20238, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 336
	{0x2023C, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 360
	{0x00220, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 384
	{0x00222, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 408
	{0x00224, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 432
	{0x00226, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 456
	{0x00228, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 480
	{0x0022A, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 504
	{0x0022C, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 528
	{0x0022E, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 552
	{0x00230, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 576
	{0x00232, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 600
	{0x00234, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 624
	{0x00236, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 648
	{0x00238, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 672
	{0x0023A, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 696
	{0x0023C, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 720
	{0x0023E, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 1},	// 744
	{0x00220, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 768
	{0x00221, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 792
	{0x00222, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 816
	{0x00223, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 840
	{0x00224, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 864
	{0x00225, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 888
	{0x00226, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 912
	{0x00227, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 936
	{0x00228, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 960
	{0x00229, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 984
	{0x0022A, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1008
	{0x0022B, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1032
	{0x0022C, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1056
	{0x0022D, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1080
	{0x0022E, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1104
	{0x0022F, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1128
	{0x00230, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1152
	{0x00231, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1176
	{0x00232, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},	// 1200
	{0x00233, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1224
	{0x00234, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1248
	{0x00235, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1272
	{0x00236, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1296
	{0x00237, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1320
	{0x00238, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1344
	{0x00239, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1368
	{0x0023a, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1392
	{0x0023b, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1416
	{0x0023c, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1440
	{0x0023d, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1464
	{0x0023e, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1488
	{0x0023f, M6_SYS_PLL_CNTL_2, M6_SYS_PLL_CNTL_3, M6_SYS_PLL_CNTL_4, 0},  // 1512
};
static unsigned setup_a9_clk_max=1512000000;
static unsigned setup_a9_clk_min=192000000;
static int __init a9_clk_max(char *str)
{

    unsigned long  clk=clkparse(str, 0);
    if(clk<SYS_PLL_TABLE_MIN || clk>SYS_PLL_TABLE_MAX)
        return 0;
    setup_a9_clk_max=clk-(clk%24000000);
    BUG_ON(setup_a9_clk_min>setup_a9_clk_max);
    return 0;
}
early_param("a9_clk_max", a9_clk_max);
static int __init a9_clk_min(char *str)
{
    unsigned long  clk = clkparse(str, 0);
    if (clk < SYS_PLL_TABLE_MIN || clk > SYS_PLL_TABLE_MAX)
        return 0;
    setup_a9_clk_min = clk - (clk % 24000000);
    BUG_ON(setup_a9_clk_min>setup_a9_clk_max);
    return 0;
}

early_param("a9_clk_min", a9_clk_min);
static int set_sys_pll(struct clk *clk, unsigned long dst)
{
	int idx;
	unsigned int curr_cntl = aml_read_reg32(P_HHI_SYS_PLL_CNTL);
	unsigned int cpu_clk_cntl = 0;

	if (dst < SYS_PLL_TABLE_MIN) dst = SYS_PLL_TABLE_MIN;
	if (dst > SYS_PLL_TABLE_MAX) dst = SYS_PLL_TABLE_MAX;

	idx = ((dst - SYS_PLL_TABLE_MIN) / 1000000) / 24;
	cpu_clk_cntl = sys_pll_settings[idx][0];

#ifdef CONFIG_CPU_FREQ_DEBUG_DETAIL
	pr_debug("CTS_CPU_CLK %ldMHz idx=%d\n", dst / 1000000, idx);
	pr_debug("CTS_CPU_CLK sys_pll_cntl=0x%x cur_cntl=0x%x\n", cpu_clk_cntl, curr_cntl);
#endif /* CONFIG_CPU_FREQ_DEBUG_DETAIL */

	if (cpu_clk_cntl != curr_cntl) {
		aml_write_reg32(P_HHI_SYS_PLL_CNTL,  sys_pll_settings[idx][0] | (1 << 29));
		aml_write_reg32(P_HHI_SYS_PLL_CNTL2, sys_pll_settings[idx][1]);
		aml_write_reg32(P_HHI_SYS_PLL_CNTL3, sys_pll_settings[idx][2]);
		aml_write_reg32(P_HHI_SYS_PLL_CNTL4, sys_pll_settings[idx][3]);
		aml_write_reg32(P_HHI_SYS_PLL_CNTL,  sys_pll_settings[idx][0]);
#if 1
		if (clk->old_rate <= dst) {
			// when increasing frequency, lpj has already been adjusted
			do {
				udelay_scaled(100, dst / 1000000, 24 /*clk_get_rate_xtal*/);
			} while ((aml_read_reg32(P_HHI_SYS_PLL_CNTL) & 0x80000000) == 0);
		} else {
			// when decreasing frequency, lpj has not yet been adjusted
			do {
				udelay_scaled(100, clk->old_rate / 1000000, 24 /*clk_get_rate_xtal*/);
			} while ((aml_read_reg32(P_HHI_SYS_PLL_CNTL) & 0x80000000) == 0);
		}
#else
		M6_PLL_WAIT_FOR_LOCK(P_HHI_SYS_PLL_CNTL);
#endif
	}
	else {
		//printk(KERN_INFO "(CTS_CPU_CLK) No Change (0x%x)\n", cpu_clk_cntl);
	}

	if (clk)
		clk->rate = (idx * 24000000) + SYS_PLL_TABLE_MIN;

#ifdef CONFIG_CPU_FREQ_DEBUG_DETAIL
	printk("CTS_CPU_CLK %ldMHz idx=%d 0x%x scale_out=%d\n", dst / 1000000, idx, cpu_clk_cntl, sys_pll_settings[idx][4]);
#endif /* CONFIG_CPU_FREQ_DEBUG_DETAIL */

	return idx;
}
static void get_a9_divid(unsigned int idx, unsigned int * scale_divn, unsigned int * scale_out)
{
	*scale_divn = sys_pll_settings[idx][5];
	*scale_out  = sys_pll_settings[idx][4];
}
#else
static int set_sys_pll(struct clk *clk, unsigned long src, unsigned long dst)
{
	unsigned int od, M, N;
	unsigned long parent_clk = 0, rate;
	rate = clk_get_rate(clk);

	if (dst > clk->max || dst < clk->min) {
		printk(KERN_ERR "SYS PLL rate (%lu) out of range [%lu, %lu]\n", dst, clk->min, clk->max);
		return -EINVAL;
	}

	if (dst != rate) {
		unsigned long vco;
		int found = 0;

		if (clk->parent)
			parent_clk = clk_get_rate(clk->parent);
		else
			printk(KERN_ERR "sys pll: no parent clock assigned!\n");

		parent_clk /= 1000000;
		dst /= 1000000;
		if (dst > 750)
			od = 1;
		else if ((dst << 1) > 750)
			od = 2;
		else
			od = 4;

		vco = dst * od;

		// vco 750M ~ 1.5G
		for (N = 1; N < 0x1F; N++) {
			for (M = 0x1FF; M > 0; M--) {
				rate = parent_clk * M / N;

				if( rate == vco) {
					found = 1;
					break;
				}
				else if (rate > vco)
					continue;
				else
					break;
			}

			if (found)
				break;
		}

		if (found) {
			unsigned int sys_clk_cntl = 0;

			if (od == 4)
				od = 2;
			else if (od == 2)
				od = 1;
			else
				od = 0;

			sys_clk_cntl = (M) | (N << 9) | (od << 16);

			M6_PLL_RESET(P_HHI_SYS_PLL_CNTL);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL2, M6_SYS_PLL_CNTL_2);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL3, M6_SYS_PLL_CNTL_3);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL4, M6_SYS_PLL_CNTL_4);
			aml_write_reg32(P_HHI_SYS_PLL_CNTL, sys_clk_cntl);
#if 1
			do {
				udelay(100);
			} while ((aml_read_reg32(P_HHI_SYS_PLL_CNTL) & 0x80000000) == 0);
#else
			M6_PLL_WAIT_FOR_LOCK(P_HHI_SYS_PLL_CNTL);
#endif
			pr_debug("(CTS_CPU_CLK) M=%d N=%d OD=%d sys_clk_cntl=0x%x\n", M, N, od, sys_clk_cntl);
		}
		else
		{
			printk(KERN_ERR "sys pll: no clock setting matched.\n");
			return -1;
		}
	}

	return 1;
}
#endif

static int set_hpll_pll(struct clk * clk, unsigned long dst)
{
	unsigned od,M,N;
	unsigned long parent_clk = 0, rate;
	rate = clk_get_rate(clk);
	if(dst > clk->max || dst < clk->min){
		printk("vid pll: invalid rate : %lu  [%lu ~ %lu]\n",dst,clk->min,clk->max);
		return -1;
	}
	if(dst != rate){
		unsigned vco;
		unsigned od_fb = 0;
		int found = 0;
		if(clk->parent)
			parent_clk = clk_get_rate(clk->parent);
		else
			printk("vid2 pll: no parent clock assigned!\n");
		parent_clk /= 1000000;
		dst /= 1000000;
		if(dst > 750)
			od = 1;
		else if( (dst * 2) > 750)
			od = 2;
		else
			od = 4;

		vco = dst * od;

		//vco 750M ~1.5G
		for(N = 1; N < 0x1F; N++){
				for(M = 0x1FF * 4; M > 0; M--){
					rate = parent_clk * M / N;
					if(rate == vco){
						if(M > 0x1FF){
							if(((M / 2) < 0x1FF) && ((M%2) == 0)){
									od_fb = 2;
									M = M / od_fb;
									found = 1;
							}else if((M%4) == 0){
								od_fb = 4;
								M = M/od_fb;
								found = 1;
							}
							else
								break;
						}
						else{
							od_fb = 1;
							found = 1;
							break;
						}
					}
					else if(rate > vco)
						continue;
					else
						break;
				}

				if(found)
					break;
		}

		if(found){
			unsigned vid_cntl = 0;
			if(od == 4)
				od = 2;
			else if(od == 2)
				od = 1;
			else
				od = 0;
			if(od_fb == 4)
				od_fb = 2;
			else if(od_fb == 2)
				od_fb = 1;
			else
				od_fb = 0;

			vid_cntl = (M) | (N <<10) | (od << 18) | (od_fb <<20);

			//VID PLL
			M6_PLL_RESET(P_HHI_VID_PLL_CNTL);
			aml_write_reg32(P_HHI_VID_PLL_CNTL2, M6_VID_PLL_CNTL_2 );
			aml_write_reg32(P_HHI_VID_PLL_CNTL3, M6_VID_PLL_CNTL_3 );
			aml_write_reg32(P_HHI_VID_PLL_CNTL4, M6_VID_PLL_CNTL_4 );
			aml_write_reg32(P_HHI_VID_PLL_CNTL,  vid_cntl );
			M6_PLL_WAIT_FOR_LOCK(P_HHI_VID_PLL_CNTL);
		}
		else
		{
			printk("vid pll: no clock setting matched.\n");
			return -1;
		}
	}

	return 1;
}
static int set_fixed_pll(struct clk * clk, unsigned long dst)
{
	if(dst == 2000000000){
		//fixed pll = xtal * M(0:8) * OD_FB(4) /N(9:13) /OD(16:17)
		//M: 0~511  OD_FB:0~1 + 1, N:0~32 + 1 OD:0~3 + 1
		//recommend this pll is fixed as 2G.
		unsigned long xtal = 24000000;
		unsigned cntl = aml_read_reg32(P_HHI_MPLL_CNTL);
		unsigned m = cntl&0x1FF;
		unsigned n = ((cntl>>9)&0x1F);
		unsigned od = ((cntl >>16)&3) + 1;
		unsigned od_fb = ((aml_read_reg32(P_HHI_MPLL_CNTL4)>>4)&1) + 1;
		unsigned long rate;
		if(clk->parent)
			xtal = clk_get_rate(clk->parent);
		xtal /= 1000000;
		rate = xtal * m * od_fb;
		rate /= n;
		rate /= od;
		rate *= 1000000;
		if(dst != rate){
			M6_PLL_RESET(P_HHI_MPLL_CNTL);
			aml_write_reg32(P_HHI_MPLL_CNTL2,M6_MPLL_CNTL_2);
			aml_write_reg32(P_HHI_MPLL_CNTL2, M6_MPLL_CNTL_2 );
			aml_write_reg32(P_HHI_MPLL_CNTL3, M6_MPLL_CNTL_3 );
			aml_write_reg32(P_HHI_MPLL_CNTL4, M6_MPLL_CNTL_4 );
			aml_write_reg32(P_HHI_MPLL_CNTL5, M6_MPLL_CNTL_5 );
			aml_write_reg32(P_HHI_MPLL_CNTL6, M6_MPLL_CNTL_6 );
			aml_write_reg32(P_HHI_MPLL_CNTL7, M6_MPLL_CNTL_7 );
			aml_write_reg32(P_HHI_MPLL_CNTL8, M6_MPLL_CNTL_8 );
			aml_write_reg32(P_HHI_MPLL_CNTL9, M6_MPLL_CNTL_9 );
			aml_write_reg32(P_HHI_MPLL_CNTL10,M6_MPLL_CNTL_10);
			aml_write_reg32(P_HHI_MPLL_CNTL, 0x67d );
			M6_PLL_WAIT_FOR_LOCK(P_HHI_MPLL_CNTL);
		}
	}
	else
		return -1;
	return 0;
}
static int set_vid2_pll(struct clk * clk, unsigned long dst)
{
	unsigned od,M,N;
	unsigned long parent_clk = 0, rate;
	rate = clk_get_rate(clk);
	if(dst > clk->max || dst < clk->min){
		printk("vid2 pll: invalid rate : %lu  [%lu ~ %lu]\n",dst,clk->min,clk->max);
		return -1;
	}
	if(dst != rate){
		unsigned vco;
		int found = 0;
		if(clk->parent)
			parent_clk = clk_get_rate(clk->parent);
		else
			printk("vid2 pll: no parent clock assigned!\n");
		parent_clk /= 1000000;
		dst /= 1000000;
		if(dst > 750)
			od = 1;
		else if( (dst * 2) > 750)
			od = 2;
		else
			od = 4;

		vco = dst * od;

		//vco 750M ~1.5G
		for(N = 1; N < 0x1F; N++){
				for(M = 0x1FF; M > 0; M--){
					rate = parent_clk * M / N;
					if(rate == vco){
						found = 1;
						break;
					}
					else if(rate > vco)
						continue;
					else
						break;
				}

				if(found)
					break;
		}

		if(found){
			unsigned viid_cntl = 0;
			if(od == 4)
				od = 2;
			else if(od == 2)
				od = 1;
			else
				od = 0;

			viid_cntl = (M) | (N <<9) | (od << 16);
			//VIID PLL
			M6_PLL_RESET(P_HHI_VIID_PLL_CNTL);
			aml_write_reg32(P_HHI_VIID_PLL_CNTL2, M6_VIID_PLL_CNTL_2 );
			aml_write_reg32(P_HHI_VIID_PLL_CNTL3, M6_VIID_PLL_CNTL_3 );
			aml_write_reg32(P_HHI_VIID_PLL_CNTL4, M6_VIID_PLL_CNTL_4 );
			aml_write_reg32(P_HHI_VIID_PLL_CNTL,  viid_cntl);
			M6_PLL_WAIT_FOR_LOCK(P_HHI_VIID_PLL_CNTL);
		}
		else
		{
			printk("vid2 pll: no clock setting matched.\n");
			return -1;
		}
	}

	return 1;
}
//------------------------------------
//return 0:not in the clock tree, 1:in the clock tree
static int clk_in_clocktree(struct clk *clktree, struct clk *clk)
{
	struct clk *p;
	int ret = 0;
	if(IS_CLK_ERR(clk) || IS_CLK_ERR(clktree))
		return 0;
	if(clktree == clk)
		return 1;
	p = (struct clk*)clktree->sibling.next;
	while(p){
		if(p == clk){
			ret = 1;
			break;
		}
		p = (struct clk*)p->sibling.next;
	}
	if(ret == 1)
		return ret;
	return clk_in_clocktree((struct clk*)clktree->child.next, clk);
}

//return 0:ok, 1:fail
static int meson_clk_register(struct clk* clk, struct clk* parent)
{
	if (clk_in_clocktree(parent,clk))
			return 0;
	mutex_lock(&clock_ops_lock);
	clk->parent = parent;
	if (parent->child.next == NULL) {
		parent->child.next = (struct list_head*)clk;
		clk->sibling.next = NULL;
		clk->sibling.prev = NULL;
	}
	else {
		struct clk* p = (       struct clk*)(parent->child.next);
		while(p->sibling.next != NULL)
			p = (       struct clk*)(p->sibling.next);
		p->sibling.next = (struct list_head*)clk;
		clk->sibling.prev = (struct list_head*)p;
		clk->sibling.next = NULL;
	}
	mutex_unlock(&clock_ops_lock);
	return 0;
}

int clk_register(struct clk *clk,const char *parent)
{
	struct clk* p = clk_get_sys(parent,0);
	if(!IS_CLK_ERR(p))
		return meson_clk_register(clk,p);
	return 1;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
		if(IS_CLK_ERR(clk))
			return;
		mutex_lock(&clock_ops_lock);
		if(clk->sibling.next){
				struct clk* pnext = (struct clk*)(clk->sibling.next);
				pnext->sibling.prev = clk->sibling.prev;
				if(clk->sibling.prev)
					((struct clk*)(clk->sibling.prev))->sibling.next = (struct list_head*)pnext;
				else
					clk->parent->child.next = (struct list_head*)pnext;

		}
		else if(clk->sibling.prev){
				struct clk* prev = (struct clk*)(clk->sibling.prev);
				prev->sibling.next = clk->sibling.next;
				if(clk->sibling.next)
					((struct clk*)(clk->sibling.next))->sibling.prev =(struct list_head*) prev;
		}
		else{
			struct clk* parent = clk->parent;
			if(parent)
				parent->child.next = NULL;
		}
		clk->sibling.next = NULL;
		clk->sibling.prev = NULL;
		mutex_unlock(&clock_ops_lock);
}
EXPORT_SYMBOL(clk_unregister);

/**
 *  Check clock status.
 *
 *  0 -- Disabled
 *  1 -- Enabled
 *  2 -- Unknown
 */
int clk_get_status(struct clk *clk)
{
	int ret = 2;
	unsigned long flags;

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->status)
		ret = clk->status(clk);
	else if (clk->clk_gate_reg_adr != 0)
		ret = ((aml_read_reg32(clk->clk_gate_reg_adr) & clk->clk_gate_reg_mask) ? 1 : 0);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_get_status);

//return: 0:success  1: fail
int clk_ops_register(struct clk *clk, struct clk_ops *ops)
{
	int found = 0;
	struct clk_ops *p;

	mutex_lock(&clock_ops_lock);
	ops->next = NULL;
	p = clk->clk_ops;
	while(p != NULL){
		if(p == ops){
			found = 1;
			break;
		}
		p = p->next;
	}

	if(found == 0){
		if(clk->clk_ops	== NULL)
			clk->clk_ops = ops;
		else{
			struct clk_ops* p = clk->clk_ops;
			while(p->next)
				p = p->next;
			p->next = ops;
		}
	}
	mutex_unlock(&clock_ops_lock);
	return 0;
}
EXPORT_SYMBOL(clk_ops_register);

//return: 0:success  1: fail
int clk_ops_unregister(struct clk *clk, struct clk_ops *ops)
{
	if(ops == NULL || IS_CLK_ERR(clk))
		return 0;

	mutex_lock(&clock_ops_lock);

	if(clk->clk_ops == ops){
		if(clk->clk_ops->next == NULL)
			clk->clk_ops = NULL;
		else
			clk->clk_ops = clk->clk_ops->next;
	}
	else if(clk->clk_ops){
		struct clk_ops *p, *p1;
		p = clk->clk_ops->next;
		p1 = clk->clk_ops;
		while(p != NULL && p != ops){
			p1 = p;
			p = p->next;
		}
		if(p == ops)
			p1->next = p->next;
	}
	ops->next = NULL;
	mutex_unlock(&clock_ops_lock);
	return 0;
}
EXPORT_SYMBOL(clk_ops_unregister);

///FIXME add data later
#define PLL_CLK_DEFINE(name,msr)    		\
	static unsigned pll_##name##_data[10];	\
    CLK_DEFINE(pll_##name,xtal,msr,set_##name##_pll, \
    		clk_msr_get,NULL,NULL,&pll_##name##_data)
_Pragma("GCC diagnostic ignored \"-Wdeclaration-after-statement\"");
#define PLL_RELATION_DEF(child,parent) meson_clk_register(&clk_pll_##child,&clk_##parent)
#define CLK_PLL_CHILD_DEF(child,parent) meson_clk_register(&clk_##child,&clk_pll_##parent)

#if 0
typedef  struct {
       unsigned int  a9_clk_cntl   ;
       unsigned char mali_clk_cntl;
}clk_cntl_t;

static int A9_ratechange_before(unsigned long newrate,void* privdata)
{
	//switch A9 input to xtal
	clk_cntl_t * pdata = (clk_cntl_t*)privdata;

       pdata->mali_clk_cntl=aml_read_reg32(P_HHI_MALI_CLK_CNTL);
       aml_clr_reg32_mask(P_HHI_MALI_CLK_CNTL,(7<<9));//mali switch to crystal
	pdata->a9_clk_cntl = aml_read_reg32(P_HHI_SYS_CPU_CLK_CNTL);
	aml_clr_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL,(1<<7));

	return 0;
}
static int A9_ratechange_after(unsigned long newrate,void* privdata,int failed)
{
	//recovery input pll.
	clk_cntl_t * pdata = (clk_cntl_t *)privdata;
	if((pdata->a9_clk_cntl & (1<<7)) != 0)
		aml_set_reg32_mask(P_HHI_SYS_CPU_CLK_CNTL,(1<<7));
       aml_set_reg32_mask(P_HHI_MALI_CLK_CNTL,(7<<9));//mali switch to fclk_div5
	return 0;
}
static int Mali_ratechange_before(unsigned long newrate,void* privdata)
{
	//switch A9 input to xtal
	clk_cntl_t * pdata = (clk_cntl_t*)privdata;
       aml_clr_reg32_mask(P_HHI_MALI_CLK_CNTL,(7<<9));//mali switch to crystal
       return 0;
}
static int Mali_ratechange_after(unsigned long newrate,void* privdata,int failed)
{
	//recovery input pll.
	clk_cntl_t * pdata = (clk_cntl_t *)privdata;

#if  defined(CONFIG_MALI_CLK_333M)
       aml_set_reg32_mask(P_HHI_MALI_CLK_CNTL,(1<<8)|(6<<9));//mali switch to fclk_div3
#elif  defined(CONFIG_MALI_CLK_400M)
       aml_set_reg32_mask(P_HHI_MALI_CLK_CNTL,(1<<8)|(7<<9));//mali switch to fclk_div5
#elif  defined(CONFIG_MALI_CLK_250M)
        aml_set_reg32_mask(P_HHI_MALI_CLK_CNTL,(1<<8)|(5<<9));//mali switch to fclk_div2
#endif

	return 0;
}

static clk_cntl_t a9_clk_cntl;
static struct clk_ops a9_clk_ops={
	//.clk_ratechange_before = A9_ratechange_before,
	//.clk_ratechange_after = A9_ratechange_after,
	.privdata = &a9_clk_cntl,
};

static clk_cntl_t mali_clk_cntl;
static struct clk_ops mali_clk_ops={
	//.clk_ratechange_before = Mali_ratechange_before,
	//.clk_ratechange_after = Mali_ratechange_after,
	.privdata = &mali_clk_cntl,
};
#endif

#ifdef CONFIG_CLKTREE_DEBUG

extern struct clk_lookup * lookup_clk(struct clk* clk);
void print_clk_name(struct clk* clk)
{
	printk("Todo: we have not lookup_clk in this kernel!\n");
	#if 0
	struct clk_lookup * p = lookup_clk(clk);
	if(p)
		printk("  %s  \n",p->dev_id);
	else
		printk(" unknown \n");
	#endif
}

void dump_child(int nlevel, struct clk* clk)
{
		if(!IS_CLK_ERR(clk)){
			int i;
			for(i = 0; i < nlevel; i++)
				printk("  ");
			print_clk_name(clk);
			dump_child(nlevel+6,(struct clk*)(clk->child.next));
			{
				struct clk * p = (struct clk*)(clk->sibling.prev);
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)(p->sibling.prev);
				}

				p = (struct clk*)(clk->sibling.next);
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)(p->sibling.next);
				}
			}
		}
}

void dump_clock_tree(struct clk* clk)
{
	printk("========= dump clock tree==============\n");
	mutex_lock(&clock_ops_lock);

	int nlevel = 0;
	if(!IS_CLK_ERR(clk)){
		print_clk_name(clk);
		dump_child(nlevel + 6,(struct clk*)(clk->child.next));
			{	int i;
				struct clk * p = (struct clk*)clk->sibling.prev;
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)clk->sibling.prev;
				}

				p = (struct clk*)clk->sibling.next;
				while(p){
					for(i = 0; i < nlevel; i++)
						printk("  ");
					print_clk_name(p);
					dump_child(nlevel+6,(struct clk*)(p->child.next));
					p = (struct clk*)clk->sibling.next;
				}
			}
	}
	mutex_unlock(&clock_ops_lock);
	printk("========= dump clock tree end ==============\n");
}

static ssize_t  clock_tree_store(struct class *cla, struct class_attribute *attr, const char *buf,size_t count)
{
	char* p = (char *)buf;
	char cmd;
	char name[20];
	unsigned long rate = 0;
	int idx = 0;
	if(count < 1)
		return -1;
	while((idx < count) && ((*p == ' ') || (*p == '\t')|| (*p == '\r') || (*p == '\n'))){
		 p++;
		 idx++;
	}

	if(idx <= count){
		int i;
		cmd = *p;
		p++;
		while((idx < count) && ((*p == ' ') || (*p == '\t')|| (*p == '\r') || (*p == '\n'))){
		 p++;
		 idx++;
		}
		i = 0;
		while((idx < count) && (*p != ' ') && (*p != '\t') && (*p != '\r') && (*p != '\n')){
			name[i++] = *p;
			p++;
			idx++;
		}
		name[i] = '\0';
		p++;
		while((idx < count) && ((*p == ' ') || (*p == '\t')|| (*p == '\r') || (*p == '\n'))){
		 p++;
		 idx++;
		}
		if(idx < count){
			int val;
			sscanf(p, "%d", &val);
			rate = val;
		}

		if(cmd == 'r'){
			if(strcmp(name,"tree") == 0){
				struct clk* clk = clk_get_sys("xtal",NULL);
				if(!IS_CLK_ERR(clk))
					dump_clock_tree(clk);
			}
			else{
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
					clk->rate = 0; //enforce update rate
					printk("%s : %lu\n",name,clk_get_rate(clk));
				}
				else
					printk("no %s in tree.\n",name);
			}
		}
		else if(cmd == 'w'){
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
					if(rate < 1000000 || rate >1512000000)
						printk("Invalid rate : %lu\n",rate);
					else{
						if(clk_set_rate(clk,rate) ==0)
							printk("%s = %lu\n",name,rate);
						else
							printk("set %s = %lu failed.\n",name,rate);
					}
				}
				else
					printk("no %s in tree.\n",name);
		}
		else if(cmd == 'o'){
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
					if(clk_enable(clk) ==0)
							printk("%s gate on\n",name);
					else
							printk("gate on %s failed.\n",name);
				}
				else
					printk("no %s in tree.\n",name);

		}
		else if(cmd == 'f'){
				struct clk* clk = clk_get_sys(name,NULL);
				if(!IS_CLK_ERR(clk)){
						clk_disable(clk);
						printk("gate off %s.\n",name);
				}
				else
					printk("no %s in tree.\n",name);
		}
		else
			printk("command:%c invalid.\n",cmd);
	}

	return count;
}

static ssize_t  clock_tree_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	printk("Usage:\n");
	printk("1. echo r tree >clkTree       ,display the clock tree.\n");
	printk("2. echo r clockname >clkTree  ,display the clock rate.\n");
	printk("3. echo w clockname rate >clkTree  ,modify the clock rate.\n");
	printk("4. echo o clockname >clkTree  ,gate on clock.\n");
	printk("5. echo f clockname >clkTree  ,gate off clock.\n");

	printk("Example:\n");
	printk("1. display the clock tree.\n");
	printk("   echo r tree >clkTree\n");
	printk("2. display clk81 rate.\n");
	printk("   echo r clk81 >clkTree\n");
	printk("3. modify sys pll as 792M.\n");
	printk("   echo w pll_sys 792000000 >clkTree\n");
	return 0;
}

static struct class_attribute clktree_class_attrs[] = {


	__ATTR(clkTree, S_IRWXU, clock_tree_show, clock_tree_store),
	__ATTR_NULL,
};

static struct class meson_clktree_class = {
	.name = "meson_clocktree",
	.class_attrs = clktree_class_attrs,
};
#endif

// -------------------- mali_max sysfs ---------------------
static ssize_t mali_max_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1 || input > 400000 || input < 83000)
		return -EINVAL;
	mali_max = input;
	return count;
}
static ssize_t mali_max_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	printk("%u\n", mali_max);
	return sprintf(buf, "%d\n", mali_max);
}

// -------------------- frequency limit sysfs ---------------------
static ssize_t freq_limit_store(struct class *cla, struct class_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	freq_limit = input;
	return count;
}
static ssize_t freq_limit_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	printk("%u\n", freq_limit);
	return sprintf(buf, "%d\n", freq_limit);
}

static struct class_attribute mali_freq_class_attrs[] = {
	__ATTR(max, S_IRWXU, mali_max_show, mali_max_store),
	__ATTR_NULL,
};

static struct class meson_mali_freq_class = {
	.name = "mali_freq",
	.class_attrs = mali_freq_class_attrs,
};

static struct class_attribute freq_limit_class_attrs[] = {
	__ATTR(limit, S_IRWXU, freq_limit_show, freq_limit_store),
	__ATTR_NULL,
};

static struct class meson_freq_limit_class = {
	.name = "freq_limit",
	.class_attrs = freq_limit_class_attrs,
};

// ------------------- /mali_max sysfs ---------------------

static int __init meson_clock_init(void)
{
	clkdev_add(&clk_lookup_xtal);
	CLK_DEFINE(pll_ddr,xtal,3,NULL,clk_msr_get,NULL,NULL,NULL);
	PLL_CLK_DEFINE(sys,(unsigned long)-1);
	PLL_CLK_DEFINE(vid2,12);
	PLL_CLK_DEFINE(fixed,-1);
	PLL_CLK_DEFINE(hpll,-1);///@todo unknown now
	clk_pll_fixed.msr_mul = 125 *2;
	clk_pll_fixed.msr_div = 3;
	clk_pll_sys.get_rate = clk_get_rate_sys;
	clk_pll_vid2.get_rate = clk_get_rate_vid2;
	clk_pll_hpll.get_rate = clk_get_rate_hpll;

	clk_pll_vid2.max = 1512000000;//1.5G
	clk_pll_vid2.min = 187500000;//187M
	clk_pll_hpll.max = 1512000000;//1.5G
	clk_pll_hpll.min = 187500000;//187M
	clk_pll_sys.max = 1512000000;//1.5G
	clk_pll_sys.min = 187500000;//187M
	clk_pll_ddr.max = 1512000000;//1.5G
	clk_pll_ddr.min = 187500000;//187M
	clk_pll_fixed.max = 2000000000; //2G
	clk_pll_fixed.min = 250000000;//250M

	//create pll tree
	PLL_RELATION_DEF(sys,xtal);
	PLL_RELATION_DEF(ddr,xtal);
	PLL_RELATION_DEF(vid2,xtal);
	PLL_RELATION_DEF(fixed,xtal);
	PLL_RELATION_DEF(hpll,xtal);

    // Add clk81
#ifdef CONFIG_CLK81_DFS
	CLK_DEFINE(clk81, pll_fixed, 7, clk_set_rate_clk81, clk_msr_get, NULL, NULL, NULL);
#else
	CLK_DEFINE(clk81, pll_fixed, 7, NULL, clk_msr_get, NULL, NULL, NULL);
#endif

	// Add clk81 as pll_fixed's child
	CLK_PLL_CHILD_DEF(clk81, fixed);

	clk_clk81.clk_gate_reg_adr = P_HHI_MPEG_CLK_CNTL;
	clk_clk81.clk_gate_reg_mask = (1<<7);
	clk_clk81.open_irq = 1;

	// Add CPU clock
	CLK_DEFINE(a9_clk, pll_sys, -1, clk_set_rate_a9, clk_get_rate_a9, NULL, NULL, NULL);
	clk_a9_clk.min = setup_a9_clk_min;
	clk_a9_clk.max = setup_a9_clk_max;
	//clk_a9_clk.open_irq = 1;
	CLK_PLL_CHILD_DEF(a9_clk,sys);

#ifdef CONFIG_HAVE_ARM_TWD
	static struct clk clk_smp_twd = {
		.set_rate =((void *)0),
		.get_rate = clk_get_rate_smp_twd,
		.enable = NULL,
		.disable = NULL,
		.priv = NULL,
		.parent = NULL,
		.msr = -1
	    };

	static struct clk_lookup clk_lookup_smp_twd = {
		.dev_id = "smp_twd",
		.con_id = NULL,
		.clk = &clk_smp_twd
	};

	clkdev_add(&clk_lookup_smp_twd);
#endif /* CONFIG_HAVE_ARM_TWD */

	// Add GPU clock
	CLK_DEFINE(mali, pll_fixed, 35, clk_set_rate_mali, clk_msr_get, NULL, NULL, &clk_a9_clk);
	clk_mali.min = 111000000;
	clk_mali.max = 400000000;
	clk_mali.enable = clk_enable_mali;
	clk_mali.disable = clk_disable_mali;
	//clk_mali.status = clk_status_mali;
	CLK_PLL_CHILD_DEF(mali, fixed);
	//clk_ops_register(&clk_mali, &mali_clk_ops);

#ifdef CONFIG_AMLOGIC_USB
	// Add clk usb0
	CLK_DEFINE(usb0,xtal,4,NULL,clk_msr_get,clk_enable_usb,clk_disable_usb,"usb0");
	meson_clk_register(&clk_usb0,&clk_xtal);
	//clk_usb0.clk_gate_reg_adr = P_USB_ADDR0;
	//clk_usb0.clk_gate_reg_mask = (1<<0);

	// Add clk usb1
	CLK_DEFINE(usb1,xtal,5,NULL,clk_msr_get,clk_enable_usb,clk_disable_usb,"usb1");
	meson_clk_register(&clk_usb1,&clk_xtal);
	//clk_usb1.clk_gate_reg_adr = P_USB_ADDR8;
	//clk_usb1.clk_gate_reg_mask = (1<<0);

	CLK_DEFINE(usb2,xtal,31,NULL,clk_msr_get,clk_enable_usb,clk_disable_usb,"usb2");
	meson_clk_register(&clk_usb2,&clk_xtal);
	//clk_usb2.clk_gate_reg_adr = P_USB_ADDR16;
	//clk_usb2.clk_gate_reg_mask = (1<<0);
#endif

	{
		// Dump clocks
		char *clks[] = {
				"xtal",
				"pll_sys",
				"pll_fixed",
				"pll_vid2",
				"pll_hpll",
				"pll_ddr",
				"a9_clk",
				"clk81",
				"usb0",
				"usb1",
				"smp_twd"
		};
		int i;
		int count = ARRAY_SIZE(clks);
		struct clk *clk;

		for (i = 0; i < count; i++) {
			char *clk_name = clks[i];

			clk = clk_get_sys(clk_name, NULL);
			if (!IS_CLK_ERR(clk))
				printk("clkrate [ %s ] : %lu\n", clk_name, clk_get_rate(clk));
		}
	}

#ifdef CONFIG_CLKTREE_DEBUG
	class_register(&meson_clktree_class);
#endif
	class_register(&meson_mali_freq_class);
	class_register(&meson_freq_limit_class);

	return 0;
}

/* initialize clocking early to be available later in the boot */
core_initcall(meson_clock_init);
