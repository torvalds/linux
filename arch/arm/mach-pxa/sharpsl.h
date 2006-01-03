/*
 * SharpSL SSP Driver
 */

struct corgissp_machinfo {
	int port;
	int cs_lcdcon;
	int cs_ads7846;
	int cs_max1111;
	int clk_lcdcon;
	int clk_ads7846;
	int clk_max1111;
};

void corgi_ssp_set_machinfo(struct corgissp_machinfo *machinfo);

/*
 * SharpSL Backlight
 */

void corgi_bl_set_intensity(int intensity);
void spitz_bl_set_intensity(int intensity);
void akita_bl_set_intensity(int intensity);

/*
 * SharpSL Touchscreen Driver
 */

unsigned long corgi_get_hsync_len(void);
unsigned long spitz_get_hsync_len(void);
void corgi_put_hsync(void);
void spitz_put_hsync(void);
void corgi_wait_hsync(void);
void spitz_wait_hsync(void);

/*
 * SharpSL Battery/PM Driver
 */

struct sharpsl_charger_machinfo {
	void (*init)(void);
	int gpio_acin;
	int gpio_batfull;
	int gpio_batlock;
	int gpio_fatal;
	int (*status_acin)(void);
	void (*discharge)(int);
	void (*discharge1)(int);
	void (*charge)(int);
	void (*chargeled)(int);
	void (*measure_temp)(int);
	void (*presuspend)(void);
	void (*postsuspend)(void);
	unsigned long (*charger_wakeup)(void);
	int (*should_wakeup)(unsigned int resume_on_alarm);
	int bat_levels;
	struct battery_thresh *bat_levels_noac;
	struct battery_thresh *bat_levels_acin;
	int status_high_acin;
	int status_low_acin;
	int status_high_noac;
	int status_low_noac;
};

struct battery_thresh {
	int voltage;
	int percentage;
};

struct battery_stat {
	int ac_status;         /* APM AC Present/Not Present */
	int mainbat_status;    /* APM Main Battery Status */
	int mainbat_percent;   /* Main Battery Percentage Charge */
	int mainbat_voltage;   /* Main Battery Voltage */
};

struct sharpsl_pm_status {
	struct device *dev;
	struct timer_list ac_timer;
	struct timer_list chrg_full_timer;

	int charge_mode;
#define CHRG_ERROR    (-1)
#define CHRG_OFF      (0)
#define CHRG_ON       (1)
#define CHRG_DONE     (2)

	unsigned int flags;
#define SHARPSL_SUSPENDED       (1 << 0)  /* Device is Suspended */
#define SHARPSL_ALARM_ACTIVE    (1 << 1)  /* Alarm is for charging event (not user) */
#define SHARPSL_BL_LIMIT        (1 << 2)  /* Backlight Intensity Limited */
#define SHARPSL_APM_QUEUED      (1 << 3)  /* APM Event Queued */
#define SHARPSL_DO_OFFLINE_CHRG (1 << 4)  /* Trigger the offline charger */

	int full_count;
	unsigned long charge_start_time;
	struct sharpsl_charger_machinfo *machinfo;
	struct battery_stat battstat;
};

extern struct sharpsl_pm_status sharpsl_pm;
extern struct battery_thresh spitz_battery_levels_acin[];
extern struct battery_thresh spitz_battery_levels_noac[];

#define READ_GPIO_BIT(x)    (GPLR(x) & GPIO_bit(x))

#define SHARPSL_LED_ERROR  2
#define SHARPSL_LED_ON     1
#define SHARPSL_LED_OFF    0

#define CHARGE_ON()         sharpsl_pm.machinfo->charge(1)
#define CHARGE_OFF()        sharpsl_pm.machinfo->charge(0)
#define CHARGE_LED_ON()     sharpsl_pm.machinfo->chargeled(SHARPSL_LED_ON)
#define CHARGE_LED_OFF()    sharpsl_pm.machinfo->chargeled(SHARPSL_LED_OFF)
#define CHARGE_LED_ERR()    sharpsl_pm.machinfo->chargeled(SHARPSL_LED_ERROR)
#define DISCHARGE_ON()      sharpsl_pm.machinfo->discharge(1)
#define DISCHARGE_OFF()     sharpsl_pm.machinfo->discharge(0)
#define STATUS_AC_IN()      sharpsl_pm.machinfo->status_acin()
#define STATUS_BATT_LOCKED()  READ_GPIO_BIT(sharpsl_pm.machinfo->gpio_batlock)
#define STATUS_CHRG_FULL()  READ_GPIO_BIT(sharpsl_pm.machinfo->gpio_batfull)
#define STATUS_FATAL()      READ_GPIO_BIT(sharpsl_pm.machinfo->gpio_fatal)
