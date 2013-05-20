#ifndef __FM_IOCTL_H__
#define __FM_IOCTL_H__
#include "fm_typedef.h"
#include "fm_rds.h"
#include "fm_main.h"

#define FM_IOC_MAGIC        0xf5 // FIXME: any conflict?

#define FM_IOCTL_POWERUP       _IOWR(FM_IOC_MAGIC, 0, struct fm_tune_parm*)
#define FM_IOCTL_POWERDOWN     _IOWR(FM_IOC_MAGIC, 1, int32_t*)
#define FM_IOCTL_TUNE          _IOWR(FM_IOC_MAGIC, 2, struct fm_tune_parm*)
#define FM_IOCTL_SEEK          _IOWR(FM_IOC_MAGIC, 3, struct fm_seek_parm*)
#define FM_IOCTL_SETVOL        _IOWR(FM_IOC_MAGIC, 4, uint32_t*)
#define FM_IOCTL_GETVOL        _IOWR(FM_IOC_MAGIC, 5, uint32_t*)
#define FM_IOCTL_MUTE          _IOWR(FM_IOC_MAGIC, 6, uint32_t*)
#define FM_IOCTL_GETRSSI       _IOWR(FM_IOC_MAGIC, 7, int32_t*)
#define FM_IOCTL_SCAN          _IOWR(FM_IOC_MAGIC, 8, struct fm_scan_parm*)
#define FM_IOCTL_STOP_SCAN     _IO(FM_IOC_MAGIC,   9)
#define FM_IOCTL_POWERUP_TX    _IOWR(FM_IOC_MAGIC, 20, struct fm_tune_parm*)
#define FM_IOCTL_TUNE_TX       _IOWR(FM_IOC_MAGIC, 21, struct fm_tune_parm*)
#define FM_IOCTL_RDS_TX        _IOWR(FM_IOC_MAGIC, 22, struct fm_rds_tx_parm*)

//IOCTL and struct for test
#define FM_IOCTL_GETCHIPID     _IOWR(FM_IOC_MAGIC, 10, uint16_t*)
#define FM_IOCTL_EM_TEST       _IOWR(FM_IOC_MAGIC, 11, struct fm_em_parm*)
#define FM_IOCTL_RW_REG        _IOWR(FM_IOC_MAGIC, 12, struct fm_ctl_parm*)
#define FM_IOCTL_GETMONOSTERO  _IOWR(FM_IOC_MAGIC, 13, uint16_t*)
#define FM_IOCTL_GETCURPAMD    _IOWR(FM_IOC_MAGIC, 14, uint16_t*)
#define FM_IOCTL_GETGOODBCNT   _IOWR(FM_IOC_MAGIC, 15, uint16_t*)
#define FM_IOCTL_GETBADBNT     _IOWR(FM_IOC_MAGIC, 16, uint16_t*)
#define FM_IOCTL_GETBLERRATIO  _IOWR(FM_IOC_MAGIC, 17, uint16_t*)

//IOCTL for RDS 
#define FM_IOCTL_RDS_ONOFF     _IOWR(FM_IOC_MAGIC, 18, uint16_t*)
#define FM_IOCTL_RDS_SUPPORT   _IOWR(FM_IOC_MAGIC, 19, int32_t*)

#define FM_IOCTL_RDS_SIM_DATA  _IOWR(FM_IOC_MAGIC, 23, uint32_t*)
#define FM_IOCTL_IS_FM_POWERED_UP  _IOWR(FM_IOC_MAGIC, 24, uint32_t*)

//IOCTL for FM Tx
#define FM_IOCTL_TX_SUPPORT    _IOWR(FM_IOC_MAGIC, 25, int32_t*)
#define FM_IOCTL_RDSTX_SUPPORT _IOWR(FM_IOC_MAGIC, 26, int32_t*)
#define FM_IOCTL_RDSTX_ENABLE  _IOWR(FM_IOC_MAGIC, 27, int32_t*)
#define FM_IOCTL_TX_SCAN       _IOWR(FM_IOC_MAGIC, 28, struct fm_tx_scan_parm*)

//IOCTL for FM over BT
#define FM_IOCTL_OVER_BT_ENABLE  _IOWR(FM_IOC_MAGIC, 29, int32_t*)

//IOCTL for FM ANTENNA SWITCH
#define FM_IOCTL_ANA_SWITCH     _IOWR(FM_IOC_MAGIC, 30, int32_t*)
#define FM_IOCTL_GETCAPARRAY  	_IOWR(FM_IOC_MAGIC, 31, int32_t*)

//IOCTL for FM compensation by GPS RTC
#define FM_IOCTL_GPS_RTC_DRIFT  _IOWR(FM_IOC_MAGIC, 32, struct fm_gps_rtc_info*)

//IOCTL for FM I2S Setting
#define FM_IOCTL_I2S_SETTING  _IOWR(FM_IOC_MAGIC, 33, struct fm_i2s_setting*)

#define FM_IOCTL_RDS_GROUPCNT   _IOWR(FM_IOC_MAGIC, 34, struct rds_group_cnt_req_t*)
#define FM_IOCTL_RDS_GET_LOG    _IOWR(FM_IOC_MAGIC, 35, struct rds_raw_t*)

#define FM_IOCTL_SCAN_GETRSSI   _IOWR(FM_IOC_MAGIC, 36, struct fm_rssi_req*)
#define FM_IOCTL_SETMONOSTERO   _IOWR(FM_IOC_MAGIC, 37, int32_t)

#define FM_IOCTL_RDS_BC_RST     _IOWR(FM_IOC_MAGIC, 38, int32_t*)
#define FM_IOCTL_CQI_GET     _IOWR(FM_IOC_MAGIC, 39, struct fm_cqi_req*)
#define FM_IOCTL_GET_HW_INFO    _IOWR(FM_IOC_MAGIC, 40, struct fm_hw_info*)
#define FM_IOCTL_GET_I2S_INFO   _IOWR(FM_IOC_MAGIC, 41, struct fm_i2s_info*)
#define FM_IOCTL_IS_DESE_CHAN   _IOWR(FM_IOC_MAGIC, 42, int32_t*)

#define FM_IOCTL_SCAN_NEW       _IOWR(FM_IOC_MAGIC, 60, struct fm_scan_t*)
#define FM_IOCTL_SEEK_NEW       _IOWR(FM_IOC_MAGIC, 61, struct fm_seek_t*)
#define FM_IOCTL_TUNE_NEW       _IOWR(FM_IOC_MAGIC, 62, struct fm_tune_t*)
#define FM_IOCTL_SOFT_MUTE_TUNE _IOWR(FM_IOC_MAGIC, 63, struct fm_softmute_tune_t*)/*for soft mute tune*/
#define FM_IOCTL_DESENSE_CHECK   _IOWR(FM_IOC_MAGIC, 64, fm_desense_check_t*)

#define FM_IOCTL_DUMP_REG   _IO(FM_IOC_MAGIC, 0xFF)

#endif //__FM_IOCTL_H__

