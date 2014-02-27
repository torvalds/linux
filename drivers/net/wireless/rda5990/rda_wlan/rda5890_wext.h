/**
  * This file contains definition for IOCTL call.
  */
#include "rda5890_defs.h"  
#ifndef	_RDA5890_WEXT_H_
#define	_RDA5890_WEXT_H_

#ifndef DEFAULT_MAX_SCAN_AGE
#define DEFAULT_MAX_SCAN_AGE (15*HZ)
#ifdef GET_SCAN_FROM_NETWORK_INFO
#else
#define DEFAULT_MAX_SCAN_AGE (15*HZ)
#endif
#else
#undef DEFAULT_MAX_SCAN_AGE
#ifdef GET_SCAN_FROM_NETWORK_INFO
#define DEFAULT_MAX_SCAN_AGE (15*HZ)
#else
#define DEFAULT_MAX_SCAN_AGE (15*HZ)
#endif
#endif

extern struct iw_handler_def rda5890_wext_handler_def;

void rda5890_indicate_disconnected(struct rda5890_private *priv);
void rda5890_indicate_connected(struct rda5890_private *priv);
void rda5890_assoc_done_worker(struct work_struct *work);
void rda5890_assoc_worker(struct work_struct *work);
void rda5890_scan_worker(struct work_struct *work);
void rda5890_wlan_connect_worker(struct work_struct *work);

static inline unsigned char  is_zero_eth_addr(unsigned char * addr)
{
    return !(addr[0] | addr[1] |addr[2] |addr[3] |addr[4] |addr[5]);
}

#endif
