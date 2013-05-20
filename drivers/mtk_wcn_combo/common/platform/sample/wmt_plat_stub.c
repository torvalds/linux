/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/




/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/


#if CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#define CFG_WMT_WAKELOCK_SUPPORT 1
#endif

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-PLAT]"


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/* ALPS and COMBO header files */
#include <mach/mtk_wcn_cmb_stub.h>

/* MTK_WCN_COMBO header files */
#include "wmt_plat.h"
#include "wmt_plat_stub.h"
#include "wmt_exp.h"
#include <mach/mtk_wcn_cmb_stub.h>

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/




/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static VOID wmt_plat_func_ctrl (UINT32 type, UINT32 on);



/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/


/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static VOID wmt_plat_func_ctrl (UINT32 type, UINT32 on)
{
    if (on) {
        mtk_wcn_wmt_func_on((ENUM_WMTDRV_TYPE_T)type);
    }
    else {
        mtk_wcn_wmt_func_off((ENUM_WMTDRV_TYPE_T)type);
    }
    return;
}


INT32 wmt_plat_stub_init (void)
{
    INT32 iRet = -1;
    CMB_STUB_CB stub_cb;
    stub_cb.aif_ctrl_cb = wmt_plat_audio_ctrl;
    stub_cb.func_ctrl_cb = wmt_plat_func_ctrl;
    stub_cb.size = sizeof(stub_cb);

    /* register to cmb_stub */
    iRet = mtk_wcn_cmb_stub_reg(&stub_cb);
    return iRet;
}


