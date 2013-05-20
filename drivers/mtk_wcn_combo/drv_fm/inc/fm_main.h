#ifndef __FM_MAIN_H__
#define __FM_MAIN_H__
#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_rds.h"
#include "fm_eint.h"
#include "fm_link.h"
#include "fm_interface.h"
#include "fm_stdlib.h"
#include "fm_private.h"

#define FM_NAME             "fm"
#define FM_DEVICE_NAME      "/dev/fm"

#define FM_VOL_MAX           0x2B	// 43 volume(0-15)
#define FM_TIMER_TIMEOUT_DEFAULT 1000
#define FM_TIMER_TIMEOUT_MIN 1000
#define FM_TIMER_TIMEOUT_MAX 1000000
//FM Tx
#define FM_TX_PWR_LEVEL_MAX  120         //FM transmitter power level, rang: 85db~120db, default 120db

#define FM_TX_PWR_CTRL_INVAL_DEFAULT 10
#define FM_TX_PWR_CTRL_INVAL_MIN 5
#define FM_TX_PWR_CTRL_INVAL_MAX 10000

#define FM_TX_VCO_OFF_DEFAULT 5
#define FM_TX_VCO_OFF_MIN 1
#define FM_TX_VCO_OFF_MAX 10000

#define FM_TX_VCO_ON_DEFAULT 100
#define FM_TX_VCO_ON_MIN 10
#define FM_TX_VCO_ON_MAX 10000

#define FM_GPS_RTC_AGE_TH       2
#define FM_GPS_RTC_DRIFT_TH     0
#define FM_GPS_RTC_TIME_DIFF_TH 10
#define FM_GPS_RTC_RETRY_CNT    1
#define FM_GPS_RTC_DRIFT_MAX 5000
enum{
    FM_GPS_RTC_INFO_OLD = 0,
    FM_GPS_RTC_INFO_NEW = 1,
    FM_GPS_RTC_INFO_MAX
};

typedef enum
{
	FM_OVER_BT_DISABLE = 0,
	FM_OVER_BT_ENABLE
}fm_over_bt_enable_state;

#define FM_RDS_ENABLE		0x01 // 1: enable RDS, 0:disable RDS
#define FM_RDS_DATA_READY   (1 << 0)

// errno
#define FM_SUCCESS      0
#define FM_FAILED       1
#define FM_EPARM        2
#define FM_BADSTATUS    3
#define FM_TUNE_FAILED  4
#define FM_SEEK_FAILED  5
#define FM_BUSY         6
#define FM_SCAN_FAILED  7

struct fm_tune_parm {
    fm_u8 err;
    fm_u8 band;
    fm_u8 space;
    fm_u8 hilo;
    fm_u16 freq; // IN/OUT parameter
};

struct fm_seek_parm {
    fm_u8 err;
    fm_u8 band;
    fm_u8 space;
    fm_u8 hilo;
    fm_u8 seekdir;
    fm_u8 seekth;
    fm_u16 freq; // IN/OUT parameter
};

#ifdef MTK_FM_50KHZ_SUPPORT
struct fm_scan_parm {
    fm_u8  err;
    fm_u8  band;
    fm_u8  space;
    fm_u8  hilo;
    fm_u16 freq; // OUT parameter
    fm_u16 ScanTBL[26]; //need no less than the chip
    fm_u16 ScanTBLSize; //IN/OUT parameter
};
#else
struct fm_scan_parm {
    fm_u8  err;
    fm_u8  band;
    fm_u8  space;
    fm_u8  hilo;
    fm_u16 freq; // OUT parameter
    fm_u16 ScanTBL[16]; //need no less than the chip
    fm_u16 ScanTBLSize; //IN/OUT parameter
};
#endif

struct fm_cqi {
    fm_s32 ch;
    fm_s32 rssi;
    fm_s32 reserve;
};

struct fm_cqi_req {
    fm_u16 ch_num;
    fm_s32 buf_size;
    fm_s8 *cqi_buf;
};

struct fm_ch_rssi {
    fm_u16 freq;
    fm_s32 rssi;
};

enum fm_scan_cmd_t {
    FM_SCAN_CMD_INIT = 0,
    FM_SCAN_CMD_START,
    FM_SCAN_CMD_GET_NUM,
    FM_SCAN_CMD_GET_CH,
    FM_SCAN_CMD_GET_RSSI,
    FM_SCAN_CMD_GET_CH_RSSI,
    FM_SCAN_CMD_MAX
};

struct fm_scan_t {
    enum fm_scan_cmd_t cmd;    
    fm_s32 ret;    // 0, success; else error code
    fm_u16 lower;             // lower band, Eg, 7600 -> 76.0Mhz
    fm_u16 upper;             // upper band, Eg, 10800 -> 108.0Mhz
    fm_s32 space;                  // 5: 50KHz, 10: 100Khz, 20: 200Khz
    fm_s32 num;                    // valid channel number 
    void *priv;
    fm_s32 sr_size;                // scan result buffer size in bytes
    union {
        fm_u16 *ch_buf;       // channel buffer
        fm_s32 *rssi_buf;          // rssi buffer
        struct fm_ch_rssi *ch_rssi_buf;  //channel and RSSI buffer 
    } sr;    
};

struct fm_seek_t {  
    fm_s32 ret;                    // 0, success; else error code
    fm_u16 freq;
    fm_u16 lower;             // lower band, Eg, 7600 -> 76.0Mhz
    fm_u16 upper;             // upper band, Eg, 10800 -> 108.0Mhz
    fm_s32 space;                  // 5: 50KHz, 10: 100Khz, 20: 200Khz
    fm_s32 dir;                    // 0: up; 1: down
    fm_s32 th;                     // seek threshold in dbm(Eg, -95dbm)
    void *priv;
};

struct fm_tune_t {  
    fm_s32 ret;                    // 0, success; else error code
    fm_u16 freq;
    fm_u16 lower;             // lower band, Eg, 7600 -> 76.0Mhz
    fm_u16 upper;             // upper band, Eg, 10800 -> 108.0Mhz
    fm_s32 space;                  // 5: 50KHz, 10: 100Khz, 20: 200Khz
    void *priv;
};


#ifdef MTK_FM_50KHZ_SUPPORT
struct fm_rssi_req {
    fm_u16 num;
    fm_u16 read_cnt;
    struct fm_ch_rssi cr[26*16];
};
#else
struct fm_rssi_req {
    fm_u16 num;
    fm_u16 read_cnt;
    struct fm_ch_rssi cr[16*16];
};
#endif

struct fm_rds_tx_parm {
    fm_u8 err;
    fm_u16 pi;
    fm_u16 ps[12]; // 4 ps
    fm_u16 other_rds[87];  // 0~29 other groups
    fm_u8 other_rds_cnt; // # of other group
};

typedef struct fm_rds_tx_req {
    unsigned char pty;         // 0~31 integer
    unsigned char rds_rbds;    // 0:RDS, 1:RBDS
    unsigned char dyn_pty;     // 0:static, 1:dynamic
    unsigned short pi_code;    // 2-byte hex
    unsigned char ps_buf[8];     // hex buf of PS
    unsigned char ps_len;      // length of PS, must be 0 / 8"
    unsigned char af;          // 0~204, 0:not used, 1~204:(87.5+0.1*af)MHz
    unsigned char ah;          // Artificial head, 0:no, 1:yes
    unsigned char stereo;      // 0:mono, 1:stereo
    unsigned char compress;    // Audio compress, 0:no, 1:yes
    unsigned char tp;          // traffic program, 0:no, 1:yes
    unsigned char ta;          // traffic announcement, 0:no, 1:yes
    unsigned char speech;      // 0:music, 1:speech
} fm_rds_tx_req;

#define TX_SCAN_MAX 10
#define TX_SCAN_MIN 1

struct fm_tx_scan_parm {
    fm_u8  err;
    fm_u8  band;	//87.6~108MHz
    fm_u8  space;
    fm_u8  hilo;
    fm_u16 freq; 	// start freq, if less than band min freq, then will use band min freq
    fm_u8 scandir;
    fm_u16 ScanTBL[TX_SCAN_MAX]; 	//need no less than the chip
    fm_u16 ScanTBLSize; //IN: desired size, OUT: scan result size
};

struct fm_gps_rtc_info {
    fm_s32             err;            //error number, 0: success, other: err code
    fm_s32             retryCnt;       //GPS mnl can decide retry times
    fm_s32             ageThd;         //GPS 3D fix time diff threshold
    fm_s32             driftThd;       //GPS RTC drift threshold
    struct timeval  tvThd;          //time value diff threshold
    fm_s32             age;            //GPS 3D fix time diff
    fm_s32             drift;          //GPS RTC drift
    union {
        unsigned long stamp;        //time stamp in jiffies
        struct timeval  tv;         //time stamp value in RTC
    };
    fm_s32             flag;           //rw flag
};

typedef enum {
    FM_I2S_ON = 0,
    FM_I2S_OFF
} fm_i2s_state;

typedef enum {
    FM_I2S_MASTER = 0,
    FM_I2S_SLAVE
} fm_i2s_mode;

typedef enum {
    FM_I2S_32K = 0,
    FM_I2S_44K,
    FM_I2S_48K
} fm_i2s_sample;

struct fm_i2s_setting {
    fm_s32 onoff;
    fm_s32 mode;
    fm_s32 sample;
};
typedef struct
{
	fm_s32 freq;
	fm_s32 rssi;
}fm_desense_check_t;

typedef enum {
    FM_RX = 0,
    FM_TX = 1
} FM_PWR_T;

struct fm_ctl_parm {
	fm_u8 err;
	fm_u8 addr;
	fm_u16 val;
	fm_u16 rw_flag;//0:write, 1:read
};
struct fm_em_parm {
	fm_u16 group_idx;
	fm_u16 item_idx;
	fm_u32 item_value;
};

enum {
    FM_SUBSYS_RST_OFF,
    FM_SUBSYS_RST_START,
    FM_SUBSYS_RST_END,
    FM_SUBSYS_RST_MAX
};
enum{
    FM_TX_PWR_CTRL_DISABLE,
    FM_TX_PWR_CTRL_ENABLE,
    FM_TX_PWR_CTRL_MAX
};

enum{
    FM_TX_RTC_CTRL_DISABLE,
    FM_TX_RTC_CTRL_ENABLE,
    FM_TX_RTC_CTRL_MAX
};

enum{
    FM_TX_DESENSE_DISABLE,
    FM_TX_DESENSE_ENABLE,
    FM_TX_DESENSE_MAX
};

struct fm_softmute_tune_t 
{  
	fm_s32 rssi;              // RSSI of current channel
	fm_u16 freq;				//current frequency
	fm_bool valid;				    //current channel is valid(true) or not(false)
};

//init and deinit APIs
extern fm_s32 fm_env_setup(void);
extern fm_s32 fm_env_destroy(void);
extern struct fm* fm_dev_init(fm_u32 arg);
extern fm_s32 fm_dev_destroy(struct fm *fm);


//fm main basic APIs
extern enum fm_pwr_state fm_pwr_state_get(struct fm *fmp);
extern enum fm_pwr_state fm_pwr_state_set(struct fm *fmp, enum fm_pwr_state sta);
extern fm_s32 fm_open(struct fm *fmp);
extern fm_s32 fm_close(struct fm *fmp);
extern fm_s32 fm_rds_read(struct fm *fmp, fm_s8 *dst, fm_s32 len);
extern fm_s32 fm_powerup(struct fm *fm, struct fm_tune_parm *parm);
extern fm_s32 fm_powerdown(struct fm *fm);
extern fm_s32 fm_seek(struct fm *fm, struct fm_seek_parm *parm);
extern fm_s32 fm_scan(struct fm *fm, struct fm_scan_parm *parm);
extern fm_s32 fm_cqi_get(struct fm *fm, fm_s32 ch_num, fm_s8 *buf, fm_s32 buf_size);
extern fm_s32 fm_get_hw_info(struct fm *pfm, struct fm_hw_info *req);
extern fm_s32 fm_hwscan_stop(struct fm *fm);
extern fm_s32 fm_ana_switch(struct fm *fm, fm_s32 antenna);
extern fm_s32 fm_setvol(struct fm *fm, fm_u32 vol);
extern fm_s32 fm_getvol(struct fm *fm, fm_u32 *vol);
extern fm_s32 fm_mute(struct fm *fm, fm_u32 bmute);
extern fm_s32 fm_getrssi(struct fm *fm, fm_s32 *rssi);
extern fm_s32 fm_reg_read(struct fm *fm, fm_u8 addr, fm_u16 *val);
extern fm_s32 fm_reg_write(struct fm *fm, fm_u8 addr, fm_u16 val);
extern fm_s32 fm_chipid_get(struct fm *fm, fm_u16 *chipid);
extern fm_s32 fm_monostereo_get(struct fm *fm, fm_u16 *ms);
extern fm_s32 fm_monostereo_set(struct fm *fm, fm_s32 ms);
extern fm_s32 fm_pamd_get(struct fm *fm, fm_u16 *pamd);
extern fm_s32 fm_caparray_get(struct fm *fm, fm_s32 *ca);
extern fm_s32 fm_em_test(struct fm *fm, fm_u16 group, fm_u16 item, fm_u32 val);
extern fm_s32 fm_rds_onoff(struct fm *fm, fm_u16 rdson_off);
extern fm_s32 fm_rds_good_bc_get(struct fm *fm, fm_u16 *gbc);
extern fm_s32 fm_rds_bad_bc_get(struct fm *fm, fm_u16 *bbc);
extern fm_s32 fm_rds_bler_ratio_get(struct fm *fm, fm_u16 *bbr);
extern fm_s32 fm_rds_group_cnt_get(struct fm *fm, struct rds_group_cnt_t *dst);
extern fm_s32 fm_rds_group_cnt_reset(struct fm *fm);
extern fm_s32 fm_rds_log_get(struct fm *fm, struct rds_rx_t *dst, fm_s32 *dst_len);
extern fm_s32 fm_rds_block_cnt_reset(struct fm *fm);
extern fm_s32 fm_i2s_set(struct fm *fm, fm_s32 onoff, fm_s32 mode, fm_s32 sample);
extern fm_s32 fm_get_i2s_info(struct fm *pfm, struct fm_i2s_info *req);
extern fm_s32 fm_tune(struct fm *fm, struct fm_tune_parm *parm);
extern fm_s32 fm_is_dese_chan(struct fm *pfm, fm_u16 freq);
extern fm_s32 fm_desense_check(struct fm *pfm, fm_u16 freq,fm_s32 rssi);
extern fm_s32 fm_sys_state_get(struct fm *fmp);
extern fm_s32 fm_sys_state_set(struct fm *fmp, fm_s32 sta);
extern fm_s32 fm_subsys_reset(struct fm *fm);

extern fm_s32 fm_scan_new(struct fm *fm, struct fm_scan_t *parm);
extern fm_s32 fm_seek_new(struct fm *fm, struct fm_seek_t *parm);
extern fm_s32 fm_tune_new(struct fm *fm, struct fm_tune_t *parm);

extern fm_s32 fm_cust_config_setup(fm_s8 * filename);
extern fm_s32 fm_cqi_log(void);
extern fm_s32 fm_soft_mute_tune(struct fm *fm, struct fm_softmute_tune_t *parm);
extern fm_s32 fm_dump_reg(void);
extern fm_s32 fm_get_gps_rtc_info(struct fm_gps_rtc_info *src);
extern fm_s32 fm_over_bt(struct fm *fm, fm_s32 flag);

/*tx function*/
extern fm_s32 fm_tx_support(struct fm *fm, fm_s32 *support);

extern fm_s32 fm_powerup_tx(struct fm *fm, struct fm_tune_parm *parm);
extern fm_s32 fm_tune_tx(struct fm *fm, struct fm_tune_parm *parm);
extern fm_s32 fm_powerdowntx(struct fm *fm);
extern fm_s32 fm_rds_tx(struct fm *fm, struct fm_rds_tx_parm *parm);
extern fm_s32 fm_rdstx_support(struct fm *fm, fm_s32 *support);
extern fm_s32 fm_rdstx_enable(struct fm *fm, fm_s32 *support);
extern fm_s32 fm_tx_scan(struct fm *fm, struct fm_tx_scan_parm *parm);

#endif //__FM_MAIN_H__

