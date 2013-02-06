#ifndef _SEC_MODEM_H_
#define _SEC_MODEM_H_

enum hsic_lpa_states {
	STATE_HSIC_LPA_ENTER,
	STATE_HSIC_LPA_WAKE,
	STATE_HSIC_LPA_PHY_INIT,
	STATE_HSIC_LPA_CHECK,
};

#if defined(CONFIG_LINK_DEVICE_HSIC) || defined(CONFIG_LINK_DEVICE_USB)
void set_host_states(struct platform_device *pdev, int type);
void set_hsic_lpa_states(int states);
int get_cp_active_state(void);
#elif defined(CONFIG_MDM_HSIC_PM)
int set_hsic_lpa_states(int states);
#else
#define set_hsic_lpa_states(states) do {} while (0);
#endif

#if defined(CONFIG_MACH_C1) || defined(CONFIG_MACH_BAFFIN)
bool modem_using_hub(void);
#else
static inline bool modem_using_hub(void) { return false; }
#endif

#endif
