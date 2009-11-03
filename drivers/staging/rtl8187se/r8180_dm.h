#ifndef R8180_DM_H
#define R8180_DM_H

#include "r8180.h"
//#include "r8180_hw.h"
//#include "r8180_93cx6.h"
void SwAntennaDiversityRxOk8185(struct net_device *dev, u8 SignalStrength);
bool SetAntenna8185(struct net_device *dev,	u8 u1bAntennaIndex);
bool SwitchAntenna(	struct net_device *dev);
void SwAntennaDiversity(struct net_device *dev	);
void SwAntennaDiversityTimerCallback(struct net_device *dev);
bool CheckDig(struct net_device *dev);
bool CheckHighPower(struct net_device *dev);
void rtl8180_hw_dig_wq (struct work_struct *work);
void rtl8180_tx_pw_wq (struct work_struct *work);
void rtl8180_rate_adapter(struct work_struct * work);
void TxPwrTracking87SE(struct net_device *dev);
bool CheckTxPwrTracking(struct net_device *dev);
void rtl8180_rate_adapter(struct work_struct * work);
void timer_rate_adaptive(unsigned long data);


#endif
