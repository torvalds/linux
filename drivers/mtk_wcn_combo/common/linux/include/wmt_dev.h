

#ifndef _WMT_DEV_H_
#define _WMT_DEV_H_


#include "osal.h"

extern VOID wmt_dev_rx_event_cb (VOID);
extern INT32 wmt_dev_rx_timeout (P_OSAL_EVENT pEvent);
extern INT32 wmt_dev_patch_get (UCHAR *pPatchName, osal_firmware **ppPatch,INT32 padSzBuf);
extern INT32 wmt_dev_patch_put(osal_firmware **ppPatch);
extern VOID wmt_dev_patch_info_free(VOID);


#endif /*_WMT_DEV_H_*/
