/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_cfg.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 14:29
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __SUPER_CFG_H__
#define __SUPER_CFG_H__


//config wakeup source for mem
#define ALLOW_DISABLE_HOSC          (1)     // if allow disable hosc

#define STANDBY_LDO1_VOL            (1300)  //LDO1 voltage value
#define STANDBY_LDO2_VOL            (3000)  //LDO2 voltage value
#define STANDBY_LDO3_VOL            (2800)  //LDO3 voltage value
#define STANDBY_LDO4_VOL            (3300)  //LDO4 voltage value
#define STANDBY_DCDC2_VOL           (700)   //DCDC2 voltage value
#define STANDBY_DCDC3_VOL           (1000)  //DCDC3 voltage value


#endif  /* __SUPER_CFG_H__ */


