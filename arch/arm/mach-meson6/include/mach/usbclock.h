#ifndef __USB_CLK_HEADER_
#define __USB_CLK_HEADER_

#include <plat/lm.h>
#include <mach/clock.h>
typedef struct usb_peri_reg {
	volatile uint32_t config;
	volatile uint32_t ctrl;
	volatile uint32_t endp_intr;
	volatile uint32_t adp_bc;
	volatile uint32_t dbg_uart;
	volatile uint32_t test;
	volatile uint32_t tune;
	volatile uint32_t reserved;
} usb_peri_reg_t;

typedef union usb_config_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned clk_en:1;
        unsigned clk_sel:3;
        unsigned clk_div:7;
        unsigned reserved:20;
        unsigned test_trig:1;
    } b;
} usb_config_data_t;

typedef union usb_ctrl_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned soft_prst:1;
        unsigned soft_hreset:1;
        unsigned ss_scaledown_mode:2;
        unsigned clk_det_rst:1;
        unsigned intr_sel:1;
        unsigned reserved:2;
        unsigned clk_detected:1;
        unsigned sof_sent_rcvd_tgl:1;
        unsigned sof_toggle_out:1;
        unsigned not_used:4;
        unsigned por:1;
        unsigned sleepm:1;
        unsigned txbitstuffennh:1;
        unsigned txbitstuffenn:1;
        unsigned commononn:1;
        unsigned refclksel:2;
        unsigned fsel:3;
        unsigned portreset:1;
        unsigned thread_id:6;
    } b;
} usb_ctrl_data_t;

typedef union usb_endp_intr_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
        unsigned int0:1;
        unsigned int1:1;
        unsigned int2:1;
        unsigned int3:1;
        unsigned int4:1;
        unsigned int5:1;
        unsigned int6:1;
        unsigned int7:1;
        unsigned int8:1;
        unsigned int9:1;
        unsigned int10:1;
        unsigned int11:1;
        unsigned int12:1;
        unsigned int13:1;
        unsigned int14:1;
        unsigned int15:1;
        unsigned int16:1;
        unsigned int17:1;
        unsigned int18:1;
        unsigned int19:1;
        unsigned int20:1;
        unsigned int21:1;
        unsigned int22:1;
        unsigned int23:1;
        unsigned int24:1;
        unsigned int25:1;
        unsigned int26:1;
        unsigned int27:1;
        unsigned int28:1;
        unsigned int29:1;
        unsigned int30:1;
        unsigned int31:1;
    } b;
} usb_endp_intr_data_t;

typedef union usb_adp_bc_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
	unsigned vbusvldextsel:1;
	unsigned vbusvldext:1;
	unsigned otgdisable:1;
	unsigned idpullup:1;
	unsigned drvvbus:1;
	unsigned adp_prb_en:1;
	unsigned adp_dischrg:1;
	unsigned adp_chrg:1;
	unsigned sessend:1;
	unsigned device_sess_vld:1;
	unsigned bvalid:1;
	unsigned avalid:1;
	unsigned iddig:1;
	unsigned vbusvalid:1;
	unsigned adp_probe:1;
	unsigned adp_sense:1;
	unsigned aca_enable:1;
	unsigned dcd_enable:1;
	unsigned vdatdetenb:1;
	unsigned vdatsrcenb:1;
	unsigned chrgsel:1;
	unsigned chg_det:1;
	unsigned aca_pin_range_c:1;
	unsigned aca_pin_range_b:1;
	unsigned aca_pin_range_a:1;
	unsigned aca_pin_gnd:1;
	unsigned aca_pin_float:1;
	unsigned not_used:5;
    } b;
} usb_adp_bc_data_t;

typedef union usb_dbg_uart_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
	unsigned bypass_sel:1;
	unsigned bypass_dm_en:1;
	unsigned bypass_dp_en:1;
	unsigned bypass_dm_data:1;
	unsigned bypass_dp_data:1;
	unsigned fsv_minus:1;
	unsigned fsv_plus:1;
	unsigned burn_in_test:1;
	unsigned loopbackenb:1;
	unsigned set_iddq:1;
	unsigned ate_reset:1;
	unsigned reserved:4;
	unsigned not_used:17;
    } b;
} usb_dbg_uart_data_t;

typedef union phy_test_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
	unsigned data_in:4;
	unsigned data_in_en:4;
	unsigned addr:4;
	unsigned data_out_sel:1;
	unsigned clk:1;
	unsigned vatestenb:2;
	unsigned data_out:4;
	unsigned not_used:12;
    } b;
} phy_test_data_t;

typedef union phy_tune_data {
    /** raw register data */
    uint32_t d32;
    /** register bits */
    struct {
	unsigned tx_res_tune:2;
	unsigned tx_hsxv_tune:2;
	unsigned tx_vref_tune:4;
	unsigned tx_rise_tune:2;
	unsigned tx_preemp_pulse_tune:1;
	unsigned tx_preemp_amp_tune:2;
	unsigned tx_fsls_tune:4;
	unsigned sqrx_tune:3;
	unsigned otg_tune:3;
	unsigned comp_dis_tune:3;
	unsigned not_used:6;
    } b;
} phy_tune_data_t;


/*
 * Clock source index must sync with chip's spec
 * M1/M2/M3/M6 are different!
 * This is only for M6
 */
#define USB_PHY_CLK_SEL_XTAL	0
#define USB_PHY_CLK_SEL_XTAL_DIV_2	1
#define USB_PHY_CLK_SEL_DDR_PLL	2
#define USB_PHY_CLK_SEL_MPLL_0	3
#define USB_PHY_CLK_SEL_MPLL_1	4
#define USB_PHY_CLK_SEL_MPLL_2	5
#define USB_PHY_CLK_SEL_FCLK_DIV_2	6
#define USB_PHY_CLK_SEL_FCLK_DIV_3	7

#define USB_BC_MODE_DISCONNECT	0	/* Disconnected */
#define USB_BC_MODE_SDP		1	/* PC */
#define USB_BC_MODE_DCP		2	/* Charger */
#define USB_BC_MODE_CDP		3	/* PC + Charger */

#define USB_CORE_RESET_TIME 	200
#define USB_ID_CHANGE_TIME	200

int clk_enable_usb(struct clk *clk);
int clk_disable_usb(struct clk *clk);
#endif
