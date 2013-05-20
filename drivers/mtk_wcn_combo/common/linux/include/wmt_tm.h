#ifndef _WMT_TM_H
#define _WMT_TM_H
#if WMT_PLAT_ALPS

#define CONFIG_THERMAL_OPEN
#if  defined(CONFIG_THERMAL) &&  defined(CONFIG_THERMAL_OPEN)

	struct wmt_thermal_ctrl_ops {
	    int (*query_temp)(void);
	    int (*set_temp)(int);
	};

	int wmt_tm_init(struct wmt_thermal_ctrl_ops *ops);
	int wmt_tm_deinit(void);
	int wmt_tm_init_rt(void);
	int wmt_tm_deinit_rt(void);
#endif

#endif
#endif
