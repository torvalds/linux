/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SMB5_CHARGER_H
#define __SMB5_CHARGER_H

#include <linux/alarmtimer.h>
#include <linux/ktime.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include <linux/extcon-provider.h>
#include <linux/usb/typec.h>
#include <linux/qti_power_supply.h>
#include "storm-watch.h"
#include "battery.h"

enum print_reason {
	PR_INTERRUPT	= BIT(0),
	PR_REGISTER	= BIT(1),
	PR_MISC		= BIT(2),
	PR_PARALLEL	= BIT(3),
	PR_OTG		= BIT(4),
	PR_WLS		= BIT(5),
};

#define DEFAULT_VOTER			"DEFAULT_VOTER"
#define USER_VOTER			"USER_VOTER"
#define PD_VOTER			"PD_VOTER"
#define DCP_VOTER			"DCP_VOTER"
#define QC_VOTER			"QC_VOTER"
#define USB_PSY_VOTER			"USB_PSY_VOTER"
#define PL_TAPER_WORK_RUNNING_VOTER	"PL_TAPER_WORK_RUNNING_VOTER"
#define USBIN_V_VOTER			"USBIN_V_VOTER"
#define CHG_STATE_VOTER			"CHG_STATE_VOTER"
#define TAPER_END_VOTER			"TAPER_END_VOTER"
#define THERMAL_DAEMON_VOTER		"THERMAL_DAEMON_VOTER"
#define DIE_TEMP_VOTER			"DIE_TEMP_VOTER"
#define BOOST_BACK_VOTER		"BOOST_BACK_VOTER"
#define MICRO_USB_VOTER			"MICRO_USB_VOTER"
#define DEBUG_BOARD_VOTER		"DEBUG_BOARD_VOTER"
#define PD_SUSPEND_SUPPORTED_VOTER	"PD_SUSPEND_SUPPORTED_VOTER"
#define PL_DELAY_VOTER			"PL_DELAY_VOTER"
#define CTM_VOTER			"CTM_VOTER"
#define SW_QC3_VOTER			"SW_QC3_VOTER"
#define AICL_RERUN_VOTER		"AICL_RERUN_VOTER"
#define SW_ICL_MAX_VOTER		"SW_ICL_MAX_VOTER"
#define PL_QNOVO_VOTER			"PL_QNOVO_VOTER"
#define QNOVO_VOTER			"QNOVO_VOTER"
#define BATT_PROFILE_VOTER		"BATT_PROFILE_VOTER"
#define OTG_DELAY_VOTER			"OTG_DELAY_VOTER"
#define USBIN_I_VOTER			"USBIN_I_VOTER"
#define WEAK_CHARGER_VOTER		"WEAK_CHARGER_VOTER"
#define PL_FCC_LOW_VOTER		"PL_FCC_LOW_VOTER"
#define WBC_VOTER			"WBC_VOTER"
#define HW_LIMIT_VOTER			"HW_LIMIT_VOTER"
#define PL_SMB_EN_VOTER			"PL_SMB_EN_VOTER"
#define FORCE_RECHARGE_VOTER		"FORCE_RECHARGE_VOTER"
#define LPD_VOTER			"LPD_VOTER"
#define FCC_STEPPER_VOTER		"FCC_STEPPER_VOTER"
#define SW_THERM_REGULATION_VOTER	"SW_THERM_REGULATION_VOTER"
#define JEITA_ARB_VOTER			"JEITA_ARB_VOTER"
#define MOISTURE_VOTER			"MOISTURE_VOTER"
#define HVDCP2_ICL_VOTER		"HVDCP2_ICL_VOTER"
#define AICL_THRESHOLD_VOTER		"AICL_THRESHOLD_VOTER"
#define USBOV_DBC_VOTER			"USBOV_DBC_VOTER"
#define CHG_TERMINATION_VOTER		"CHG_TERMINATION_VOTER"
#define THERMAL_THROTTLE_VOTER		"THERMAL_THROTTLE_VOTER"
#define VOUT_VOTER			"VOUT_VOTER"
#define USB_SUSPEND_VOTER		"USB_SUSPEND_VOTER"
#define CHARGER_TYPE_VOTER		"CHARGER_TYPE_VOTER"
#define HDC_IRQ_VOTER			"HDC_IRQ_VOTER"
#define DETACH_DETECT_VOTER		"DETACH_DETECT_VOTER"
#define CC_MODE_VOTER			"CC_MODE_VOTER"
#define MAIN_FCC_VOTER			"MAIN_FCC_VOTER"
#define DCIN_AICL_VOTER			"DCIN_AICL_VOTER"
#define WLS_PL_CHARGING_VOTER		"WLS_PL_CHARGING_VOTER"
#define ICL_CHANGE_VOTER		"ICL_CHANGE_VOTER"
#define OVERHEAT_LIMIT_VOTER		"OVERHEAT_LIMIT_VOTER"
#define TYPEC_SWAP_VOTER		"TYPEC_SWAP_VOTER"

#define BOOST_BACK_STORM_COUNT	3
#define WEAK_CHG_STORM_COUNT	8

#define VBAT_TO_VRAW_ADC(v)		div_u64((u64)v * 1000000UL, 194637UL)

#define ITERM_LIMITS_PMI632_MA		5000
#define ITERM_LIMITS_PM8150B_MA		10000
#define ADC_CHG_ITERM_MASK		32767

#define SDP_100_MA			100000
#define SDP_CURRENT_UA			500000
#define CDP_CURRENT_UA			1500000
#define DCP_CURRENT_UA			1500000
#define HVDCP_CURRENT_UA		3000000
#define TYPEC_DEFAULT_CURRENT_UA	900000
#define TYPEC_MEDIUM_CURRENT_UA		1500000
#define TYPEC_HIGH_CURRENT_UA		3000000
#define DCIN_ICL_MIN_UA			100000
#define DCIN_ICL_MAX_UA			1500000
#define DCIN_ICL_STEP_UA		100000
#define ROLE_REVERSAL_DELAY_MS		500

enum smb_mode {
	PARALLEL_MASTER = 0,
	PARALLEL_SLAVE,
	NUM_MODES,
};

enum sink_src_mode {
	SINK_MODE,
	SRC_MODE,
	AUDIO_ACCESS_MODE,
	UNATTACHED_MODE,
};

enum qc2_non_comp_voltage {
	QC2_COMPLIANT,
	QC2_NON_COMPLIANT_9V,
	QC2_NON_COMPLIANT_12V
};

enum {
	BOOST_BACK_WA			= BIT(0),
	SW_THERM_REGULATION_WA		= BIT(1),
	WEAK_ADAPTER_WA			= BIT(2),
	USBIN_OV_WA			= BIT(3),
	CHG_TERMINATION_WA		= BIT(4),
	USBIN_ADC_WA			= BIT(5),
	SKIP_MISC_PBS_IRQ_WA		= BIT(6),
};

enum jeita_cfg_stat {
	JEITA_CFG_NONE = 0,
	JEITA_CFG_FAILURE,
	JEITA_CFG_COMPLETE,
};

enum {
	RERUN_AICL = 0,
	RESTART_AICL,
};

enum smb_irq_index {
	/* CHGR */
	CHGR_ERROR_IRQ = 0,
	CHG_STATE_CHANGE_IRQ,
	STEP_CHG_STATE_CHANGE_IRQ,
	STEP_CHG_SOC_UPDATE_FAIL_IRQ,
	STEP_CHG_SOC_UPDATE_REQ_IRQ,
	FG_FVCAL_QUALIFIED_IRQ,
	VPH_ALARM_IRQ,
	VPH_DROP_PRECHG_IRQ,
	/* DCDC */
	OTG_FAIL_IRQ,
	OTG_OC_DISABLE_SW_IRQ,
	OTG_OC_HICCUP_IRQ,
	BSM_ACTIVE_IRQ,
	HIGH_DUTY_CYCLE_IRQ,
	INPUT_CURRENT_LIMITING_IRQ,
	CONCURRENT_MODE_DISABLE_IRQ,
	SWITCHER_POWER_OK_IRQ,
	/* BATIF */
	BAT_TEMP_IRQ,
	ALL_CHNL_CONV_DONE_IRQ,
	BAT_OV_IRQ,
	BAT_LOW_IRQ,
	BAT_THERM_OR_ID_MISSING_IRQ,
	BAT_TERMINAL_MISSING_IRQ,
	BUCK_OC_IRQ,
	VPH_OV_IRQ,
	/* USB */
	USBIN_COLLAPSE_IRQ,
	USBIN_VASHDN_IRQ,
	USBIN_UV_IRQ,
	USBIN_OV_IRQ,
	USBIN_PLUGIN_IRQ,
	USBIN_REVI_CHANGE_IRQ,
	USBIN_SRC_CHANGE_IRQ,
	USBIN_ICL_CHANGE_IRQ,
	/* DC */
	DCIN_VASHDN_IRQ,
	DCIN_UV_IRQ,
	DCIN_OV_IRQ,
	DCIN_PLUGIN_IRQ,
	DCIN_REVI_IRQ,
	DCIN_PON_IRQ,
	DCIN_EN_IRQ,
	/* TYPEC */
	TYPEC_OR_RID_DETECTION_CHANGE_IRQ,
	TYPEC_VPD_DETECT_IRQ,
	TYPEC_CC_STATE_CHANGE_IRQ,
	TYPEC_VCONN_OC_IRQ,
	TYPEC_VBUS_CHANGE_IRQ,
	TYPEC_ATTACH_DETACH_IRQ,
	TYPEC_LEGACY_CABLE_DETECT_IRQ,
	TYPEC_TRY_SNK_SRC_DETECT_IRQ,
	/* MISC */
	WDOG_SNARL_IRQ,
	WDOG_BARK_IRQ,
	AICL_FAIL_IRQ,
	AICL_DONE_IRQ,
	SMB_EN_IRQ,
	IMP_TRIGGER_IRQ,
	TEMP_CHANGE_IRQ,
	TEMP_CHANGE_SMB_IRQ,
	/* FLASH */
	VREG_OK_IRQ,
	ILIM_S2_IRQ,
	ILIM_S1_IRQ,
	VOUT_DOWN_IRQ,
	VOUT_UP_IRQ,
	FLASH_STATE_CHANGE_IRQ,
	TORCH_REQ_IRQ,
	FLASH_EN_IRQ,
	SDAM_STS_IRQ,
	/* END */
	SMB_IRQ_MAX,
};

enum float_options {
	FLOAT_DCP		= 1,
	FLOAT_SDP		= 2,
	DISABLE_CHARGING	= 3,
	SUSPEND_INPUT		= 4,
};

enum chg_term_config_src {
	ITERM_SRC_UNSPECIFIED,
	ITERM_SRC_ADC,
	ITERM_SRC_ANALOG
};

enum comp_clamp_levels {
	CLAMP_LEVEL_DEFAULT = 0,
	CLAMP_LEVEL_1,
	MAX_CLAMP_LEVEL,
};

struct clamp_config {
	u16 reg[3];
	u16 val[3];
};

struct smb_irq_info {
	const char			*name;
	const irq_handler_t		handler;
	const bool			wake;
	const struct storm_watch	storm_data;
	struct smb_irq_data		*irq_data;
	int				irq;
	bool				enabled;
};

static const unsigned int smblib_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

enum lpd_reason {
	LPD_NONE,
	LPD_MOISTURE_DETECTED,
	LPD_FLOATING_CABLE,
};

/* Following states are applicable only for floating cable during LPD */
enum lpd_stage {
	/* initial stage */
	LPD_STAGE_NONE,
	/* started and ongoing */
	LPD_STAGE_FLOAT,
	/* cancel if started,  or don't start */
	LPD_STAGE_FLOAT_CANCEL,
	/* confirmed and mitigation measures taken for 60 s */
	LPD_STAGE_COMMIT,
};

enum thermal_status_levels {
	TEMP_SHUT_DOWN = 0,
	TEMP_SHUT_DOWN_SMB,
	TEMP_ALERT_LEVEL,
	TEMP_ABOVE_RANGE,
	TEMP_WITHIN_RANGE,
	TEMP_BELOW_RANGE,
};

enum icl_override_mode {
	/* APSD/Type-C/QC auto */
	HW_AUTO_MODE,
	/* 100/150/500/900mA */
	SW_OVERRIDE_USB51_MODE,
	/* ICL other than USB51 */
	SW_OVERRIDE_HC_MODE,
};

/* EXTCON_USB and EXTCON_USB_HOST are mutually exclusive */
static const u32 smblib_extcon_exclusive[] = {0x3, 0};

struct smb_regulator {
	struct regulator_dev	*rdev;
	struct regulator_desc	rdesc;
};

struct smb_irq_data {
	void			*parent_data;
	const char		*name;
	struct storm_watch	storm_data;
};

struct smb_chg_param {
	const char	*name;
	u16		reg;
	int		min_u;
	int		max_u;
	int		step_u;
	int		(*get_proc)(struct smb_chg_param *param,
				    u8 val_raw);
	int		(*set_proc)(struct smb_chg_param *param,
				    int val_u,
				    u8 *val_raw);
};

struct buck_boost_freq {
	int freq_khz;
	u8 val;
};

struct smb_chg_freq {
	unsigned int		freq_5V;
	unsigned int		freq_6V_8V;
	unsigned int		freq_9V;
	unsigned int		freq_12V;
	unsigned int		freq_removal;
	unsigned int		freq_below_otg_threshold;
	unsigned int		freq_above_otg_threshold;
};

struct smb_params {
	struct smb_chg_param	fcc;
	struct smb_chg_param	fv;
	struct smb_chg_param	usb_icl;
	struct smb_chg_param	icl_max_stat;
	struct smb_chg_param	icl_stat;
	struct smb_chg_param	otg_cl;
	struct smb_chg_param	dc_icl;
	struct smb_chg_param	jeita_cc_comp_hot;
	struct smb_chg_param	jeita_cc_comp_cold;
	struct smb_chg_param	freq_switcher;
	struct smb_chg_param	aicl_5v_threshold;
	struct smb_chg_param	aicl_cont_threshold;
};

struct parallel_params {
	struct power_supply	*psy;
};

struct smb_iio {
	struct iio_channel	*temp_chan;
	struct iio_channel	*usbin_i_chan;
	struct iio_channel	*usbin_v_chan;
	struct iio_channel	*mid_chan;
	struct iio_channel	*batt_i_chan;
	struct iio_channel	*connector_temp_chan;
	struct iio_channel	*sbux_chan;
	struct iio_channel	*vph_v_chan;
	struct iio_channel	*die_temp_chan;
	struct iio_channel	*skin_temp_chan;
	struct iio_channel	*smb_temp_chan;
};

enum pmic_type {
	PM8150B,
	PM7250B,
	PM6150,
	PMI632,
};

struct smb_charger {
	struct device		*dev;
	char			*name;
	struct regmap		*regmap;
	struct smb_irq_info	*irq_info;
	struct smb_params	param;
	struct smb_iio		iio;
	struct iio_channel	*iio_chans;
	struct iio_channel	**iio_chan_list_qg;
	struct iio_channel	**iio_chan_list_cp;
	struct iio_channel	**iio_chan_list_smb_parallel;
	int			*debug_mask;
	int			pd_disabled;
	enum smb_mode		mode;
	struct smb_chg_freq	chg_freq;
	int			otg_delay_ms;
	int			weak_chg_icl_ua;
	u32			sdam_base;
	bool			pd_not_supported;

	/* locks */
	struct mutex		smb_lock;
	struct mutex		ps_change_lock;
	struct mutex		irq_status_lock;
	struct mutex		dcin_aicl_lock;
	spinlock_t		typec_pr_lock;
	struct mutex		adc_lock;
	struct mutex		dpdm_lock;
	struct mutex		typec_lock;

	/* power supplies */
	struct power_supply		*batt_psy;
	struct power_supply		*usb_psy;
	struct power_supply		*dc_psy;
	struct power_supply		*usb_port_psy;
	struct power_supply		*wls_psy;

	/* notifiers */
	struct notifier_block	nb;

	/* parallel charging */
	struct parallel_params	pl;

	/* CC Mode */
	int	adapter_cc_mode;
	int	thermal_overheat;

	/* regulators */
	struct smb_regulator	*vbus_vreg;
	struct smb_regulator	*vconn_vreg;
	struct regulator	*dpdm_reg;

	/* typec */
	struct typec_port	*typec_port;
	struct typec_capability	typec_caps;
	struct typec_partner	*typec_partner;
	struct typec_partner_desc typec_partner_desc;

	/* votables */
	struct votable		*dc_suspend_votable;
	struct votable		*fcc_votable;
	struct votable		*fcc_main_votable;
	struct votable		*fv_votable;
	struct votable		*usb_icl_votable;
	struct votable		*awake_votable;
	struct votable		*pl_disable_votable;
	struct votable		*chg_disable_votable;
	struct votable		*pl_enable_votable_indirect;
	struct votable		*cp_disable_votable;
	struct votable		*cp_ilim_votable;
	struct votable		*smb_override_votable;
	struct votable		*icl_irq_disable_votable;
	struct votable		*limited_irq_disable_votable;
	struct votable		*hdc_irq_disable_votable;
	struct votable		*temp_change_irq_disable_votable;
	struct votable		*bat_temp_irq_disable_votable;
	struct votable		*qnovo_disable_votable;

	/* work */
	struct work_struct	bms_update_work;
	struct work_struct	pl_update_work;
	struct work_struct	jeita_update_work;
	struct work_struct	moisture_protection_work;
	struct work_struct	chg_termination_work;
	struct work_struct	dcin_aicl_work;
	struct work_struct	cp_status_change_work;
	struct delayed_work	ps_change_timeout_work;
	struct delayed_work	clear_hdc_work;
	struct delayed_work	icl_change_work;
	struct delayed_work	pl_enable_work;
	struct delayed_work	uusb_otg_work;
	struct delayed_work	bb_removal_work;
	struct delayed_work	lpd_ra_open_work;
	struct delayed_work	lpd_detach_work;
	struct delayed_work	thermal_regulation_work;
	struct delayed_work	usbov_dbc_work;
	struct delayed_work	pr_swap_detach_work;
	struct delayed_work	pr_lock_clear_work;
	struct delayed_work	role_reversal_check;

	struct alarm		lpd_recheck_timer;
	struct alarm		moisture_protection_alarm;
	struct alarm		chg_termination_alarm;
	struct alarm		dcin_aicl_alarm;

	struct timer_list	apsd_timer;

	struct charger_param	chg_param;
	/* secondary charger config */
	bool			sec_pl_present;
	bool			sec_cp_present;
	int			sec_chg_selected;
	int			cp_reason;
	int			cp_topo;

	/* pd */
	int			voltage_min_uv;
	int			voltage_max_uv;
	int			pd_active;
	bool			pd_hard_reset;
	bool			pr_lock_in_progress;
	bool			pr_swap_in_progress;
	bool			early_usb_attach;
	bool			ok_to_pd;
	bool			typec_legacy;
	bool			typec_irq_en;
	bool			typec_role_swap_failed;

	/* cached status */
	bool			system_suspend_supported;
	int			boost_threshold_ua;
	int			system_temp_level;
	int			thermal_levels;
	int			*thermal_mitigation;
	int			dcp_icl_ua;
	int			fake_capacity;
	int			fake_batt_status;
	bool			step_chg_enabled;
	bool			sw_jeita_enabled;
	bool			jeita_arb_enable;
	bool			typec_legacy_use_rp_icl;
	bool			is_hdc;
	bool			chg_done;
	int			connector_type;
	bool			otg_en;
	bool			suspend_input_on_debug_batt;
	bool			fake_chg_status_on_debug_batt;
	int			default_icl_ua;
	int			otg_cl_ua;
	bool			uusb_apsd_rerun_done;
	bool			typec_present;
	int			fake_input_current_limited;
	int			typec_mode;
	int			dr_mode;
	int			usb_icl_change_irq_enabled;
	u32			jeita_status;
	u8			float_cfg;
	bool			jeita_arb_flag;
	bool			use_extcon;
	bool			otg_present;
	bool			hvdcp_disable;
	int			hw_max_icl_ua;
	int			auto_recharge_soc;
	enum sink_src_mode	sink_src_mode;
	enum power_supply_typec_power_role power_role;
	enum jeita_cfg_stat	jeita_configured;
	int			charger_temp_max;
	int			smb_temp_max;
	u8			typec_try_mode;
	enum lpd_stage		lpd_stage;
	bool			lpd_disabled;
	enum lpd_reason		lpd_reason;
	bool			fcc_stepper_enable;
	int			die_temp;
	int			smb_temp;
	int			skin_temp;
	int			connector_temp;
	int			thermal_status;
	int			main_fcc_max;
	u32			jeita_soft_thlds[2];
	u32			jeita_soft_hys_thlds[2];
	int			jeita_soft_fcc[2];
	int			jeita_soft_fv[2];
	bool			moisture_present;
	bool			uusb_moisture_protection_capable;
	bool			uusb_moisture_protection_enabled;
	bool			hw_die_temp_mitigation;
	bool			hw_connector_mitigation;
	bool			hw_skin_temp_mitigation;
	bool			en_skin_therm_mitigation;
	int			connector_pull_up;
	int			smb_pull_up;
	int			aicl_5v_threshold_mv;
	int			default_aicl_5v_threshold_mv;
	int			aicl_cont_threshold_mv;
	int			default_aicl_cont_threshold_mv;
	bool			aicl_max_reached;
	int			charge_full_cc;
	int			cc_soc_ref;
	int			last_cc_soc;
	int			term_vbat_uv;
	int			usbin_forced_max_uv;
	int			init_thermal_ua;
	u32			comp_clamp_level;
	int			wls_icl_ua;
	int			cutoff_count;
	bool			dcin_aicl_done;
	bool			hvdcp3_standalone_config;
	bool			dcin_icl_user_set;
	bool			dpdm_enabled;
	bool			apsd_ext_timeout;
	bool			qc3p5_detected;
	int			qc3p5_detected_mw;
	bool			disable_suspend_on_collapse;

	/* workaround flag */
	int		real_charger_type;
	u32			wa_flags;
	int			boost_current_ua;
	int                     qc2_max_pulses;
	enum qc2_non_comp_voltage qc2_unsupported_voltage;
	bool			dbc_usbov;

	/* extcon for VBUS / ID notification to USB for uUSB */
	struct extcon_dev	*extcon;

	/* battery profile */
	int			batt_profile_fcc_ua;
	int			batt_profile_fv_uv;

	int			usb_icl_delta_ua;
	int			pulse_cnt;

	int			die_health;
	int			connector_health;

	/* flash */
	u32			flash_derating_soc;
	u32			flash_disable_soc;
	u32			headroom_mode;
	bool			flash_init_done;
	bool			flash_active;
	u32			irq_status;

	/* wireless */
	int			dcin_uv_count;
	ktime_t			dcin_uv_last_time;
	int			last_wls_vout;
};

int smblib_read(struct smb_charger *chg, u16 addr, u8 *val);
int smblib_masked_write(struct smb_charger *chg, u16 addr, u8 mask, u8 val);
int smblib_write(struct smb_charger *chg, u16 addr, u8 val);
int smblib_batch_write(struct smb_charger *chg, u16 addr, u8 *val, int count);
int smblib_batch_read(struct smb_charger *chg, u16 addr, u8 *val, int count);

int smblib_get_charge_param(struct smb_charger *chg,
			    struct smb_chg_param *param, int *val_u);
int smblib_get_aicl_cont_threshold(struct smb_chg_param *param, u8 val_raw);
int smblib_enable_charging(struct smb_charger *chg, bool enable);
int smblib_set_charge_param(struct smb_charger *chg,
			    struct smb_chg_param *param, int val_u);

int smblib_set_chg_freq(struct smb_chg_param *param,
				int val_u, u8 *val_raw);
int smblib_set_aicl_cont_threshold(struct smb_chg_param *param,
				int val_u, u8 *val_raw);
int smblib_vbus_regulator_enable(struct regulator_dev *rdev);
int smblib_vbus_regulator_disable(struct regulator_dev *rdev);
int smblib_vbus_regulator_is_enabled(struct regulator_dev *rdev);

int smblib_vconn_regulator_enable(struct regulator_dev *rdev);
int smblib_vconn_regulator_disable(struct regulator_dev *rdev);
int smblib_vconn_regulator_is_enabled(struct regulator_dev *rdev);

irqreturn_t smb5_default_irq_handler(int irq, void *data);
irqreturn_t smb5_smb_en_irq_handler(int irq, void *data);
irqreturn_t smb5_chg_state_change_irq_handler(int irq, void *data);
irqreturn_t smb5_batt_temp_changed_irq_handler(int irq, void *data);
irqreturn_t smb5_batt_psy_changed_irq_handler(int irq, void *data);
irqreturn_t smb5_usbin_uv_irq_handler(int irq, void *data);
irqreturn_t smb5_usb_plugin_irq_handler(int irq, void *data);
irqreturn_t smb5_usb_source_change_irq_handler(int irq, void *data);
irqreturn_t smb5_icl_change_irq_handler(int irq, void *data);
irqreturn_t smb5_typec_state_change_irq_handler(int irq, void *data);
irqreturn_t smb5_typec_attach_detach_irq_handler(int irq, void *data);
irqreturn_t smb5_dcin_uv_irq_handler(int irq, void *data);
irqreturn_t smb5_dc_plugin_irq_handler(int irq, void *data);
irqreturn_t smb5_high_duty_cycle_irq_handler(int irq, void *data);
irqreturn_t smb5_switcher_power_ok_irq_handler(int irq, void *data);
irqreturn_t smb5_wdog_snarl_irq_handler(int irq, void *data);
irqreturn_t smb5_wdog_bark_irq_handler(int irq, void *data);
irqreturn_t smb5_typec_or_rid_detection_change_irq_handler(int irq, void *data);
irqreturn_t smb5_temp_change_irq_handler(int irq, void *data);
irqreturn_t smb5_usbin_ov_irq_handler(int irq, void *data);
irqreturn_t smb5_sdam_sts_change_irq_handler(int irq, void *data);
int smblib_get_prop_input_suspend(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_batt_present(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_batt_capacity(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_batt_status(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_batt_charge_type(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_batt_charge_done(struct smb_charger *chg,
				int *val);
int smblib_get_batt_current_now(struct smb_charger *chg,
					union power_supply_propval *val);
int smblib_get_prop_batt_health(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_system_temp_level(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_system_temp_level_max(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_input_current_limited(struct smb_charger *chg,
				int *val);
int smblib_get_prop_batt_iterm(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_set_prop_input_suspend(struct smb_charger *chg,
				  const union power_supply_propval *val);
int smblib_set_prop_batt_capacity(struct smb_charger *chg,
				const union power_supply_propval *val);
int smblib_set_prop_batt_status(struct smb_charger *chg,
				const union power_supply_propval *val);
int smblib_set_prop_system_temp_level(struct smb_charger *chg,
				const union power_supply_propval *val);
int smblib_set_prop_input_current_limited(struct smb_charger *chg,
				int val);

int smblib_get_prop_dc_present(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_dc_online(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_dc_current_max(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_set_prop_dc_current_max(struct smb_charger *chg,
				const union power_supply_propval *val);
int smblib_get_prop_dc_voltage_max(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_set_prop_dc_reset(struct smb_charger *chg);
int smblib_get_prop_usb_present(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_usb_online(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_usb_online(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_usb_suspend(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_usb_voltage_max(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_usb_voltage_max_design(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_set_prop_usb_voltage_max_limit(struct smb_charger *chg,
				int val);
int smblib_get_prop_usb_voltage_now(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_low_power(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_usb_current_now(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_usb_prop_typec_mode(struct smb_charger *chg,
				int *val);
int smblib_get_usb_prop_typec_accessory_mode(struct smb_charger *chg,
				int *val);
int smblib_get_prop_typec_cc_orientation(struct smb_charger *chg,
				int *val);
int smblib_get_prop_scope(struct smb_charger *chg,
			union power_supply_propval *val);
int smblib_get_prop_typec_select_rp(struct smb_charger *chg,
				int *val);
int smblib_get_prop_typec_power_role(struct smb_charger *chg,
				int *val);
int smblib_get_prop_input_current_settled(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_input_voltage_settled(struct smb_charger *chg,
				int *val);
int smblib_get_prop_pd_in_hard_reset(struct smb_charger *chg,
			       int *val);
int smblib_get_pe_start(struct smb_charger *chg,
			       int *val);
int smblib_get_prop_charger_temp(struct smb_charger *chg,
				int *val);
int smblib_get_die_health(struct smb_charger *chg,
				int *val);
int smblib_get_prop_smb_health(struct smb_charger *chg);
int smblib_get_prop_connector_health(struct smb_charger *chg);
int smblib_get_prop_input_current_max(struct smb_charger *chg,
				  union power_supply_propval *val);
int smblib_set_prop_thermal_overheat(struct smb_charger *chg,
			       int therm_overheat);
int smblib_get_skin_temp_status(struct smb_charger *chg);
int smblib_get_prop_vph_voltage_now(struct smb_charger *chg,
				int *val);
int smb5_set_prop_comp_clamp_level(struct smb_charger *chg,
				int val);
int smblib_set_prop_pd_current_max(struct smb_charger *chg,
				int val);
int smblib_set_prop_sdp_current_max(struct smb_charger *chg,
				int val);
int smblib_set_prop_pd_voltage_max(struct smb_charger *chg,
				int val);
int smblib_set_prop_pd_voltage_min(struct smb_charger *chg,
				int val);
int smblib_set_prop_typec_power_role(struct smb_charger *chg,
				int val);
int smblib_set_prop_typec_select_rp(struct smb_charger *chg,
				int val);
int smblib_set_prop_pd_active(struct smb_charger *chg,
				int val);
int smblib_set_prop_pd_in_hard_reset(struct smb_charger *chg,
				int val);
int smblib_set_prop_ship_mode(struct smb_charger *chg,
				int val);
int smblib_set_prop_rechg_soc_thresh(struct smb_charger *chg,
				int val);
void smblib_config_charger_on_debug_battery(struct smb_charger *chg);
int smblib_rerun_apsd_if_required(struct smb_charger *chg);
void smblib_rerun_apsd(struct smb_charger *chg);
int smblib_get_prop_fcc_delta(struct smb_charger *chg,
				int *val);
int smblib_get_thermal_threshold(struct smb_charger *chg, u16 addr, int *val);
int smblib_dp_dm(struct smb_charger *chg, int val);
int smblib_disable_hw_jeita(struct smb_charger *chg, bool disable);
int smblib_run_aicl(struct smb_charger *chg, int type);
int smblib_set_icl_current(struct smb_charger *chg, int icl_ua);
int smblib_get_icl_current(struct smb_charger *chg, int *icl_ua);
int smblib_get_charge_current(struct smb_charger *chg, int *total_current_ua);
int smblib_get_prop_pr_swap_in_progress(struct smb_charger *chg,
				int *val);
int smblib_set_prop_pr_swap_in_progress(struct smb_charger *chg,
				int val);
int smblib_typec_port_type_set(const struct typec_capability *cap,
					enum typec_port_type type);
int smblib_get_prop_from_bms(struct smb_charger *chg,
				int channel, int *val);
int smblib_get_iio_channel(struct smb_charger *chg, const char *propname,
					struct iio_channel **chan);
int smblib_configure_hvdcp_apsd(struct smb_charger *chg, bool enable);
int smblib_icl_override(struct smb_charger *chg, enum icl_override_mode mode);
enum alarmtimer_restart smblib_lpd_recheck_timer(struct alarm *alarm,
				ktime_t time);
int smblib_toggle_smb_en(struct smb_charger *chg, int toggle);
void smblib_hvdcp_detect_enable(struct smb_charger *chg, bool enable);
void smblib_hvdcp_hw_inov_enable(struct smb_charger *chg, bool enable);
void smblib_hvdcp_exit_config(struct smb_charger *chg);
void smblib_apsd_enable(struct smb_charger *chg, bool enable);
int smblib_force_vbus_voltage(struct smb_charger *chg, u8 val);
int smblib_get_irq_status(struct smb_charger *chg,
				int *val);
int smblib_get_qc3_main_icl_offset(struct smb_charger *chg, int *offset_ua);

int smblib_init(struct smb_charger *chg);
int smblib_deinit(struct smb_charger *chg);
int smb5_iio_get_prop(struct smb_charger *chg, int channel, int *val);
int smb5_iio_set_prop(struct smb_charger *chg, int channel, int val);

int smblib_set_prop_voltage_wls_output(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_voltage_wls_output(struct smb_charger *chg,
				union power_supply_propval *val);
int smblib_get_prop_dc_voltage_now(struct smb_charger *chg,
				union power_supply_propval *val);

void smblib_moisture_detection_enable(struct smb_charger *chg, int pval);
#endif /* __SMB5_CHARGER_H */
