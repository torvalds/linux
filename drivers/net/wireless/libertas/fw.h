/**
  * This header file contains FW interface related definitions.
  */
#ifndef _WLAN_FW_H_
#define _WLAN_FW_H_

#ifndef DEV_NAME_LEN
#define DEV_NAME_LEN            32
#endif

int libertas_init_fw(wlan_private * priv);

#endif				/* _WLAN_FW_H_ */
