#ifndef CSR_PRIM_DEFS_H__
#define CSR_PRIM_DEFS_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/************************************************************************************
 * Segmentation of primitives in upstream and downstream segment
 ************************************************************************************/
typedef u16 CsrPrim;
#define CSR_PRIM_UPSTREAM                   ((CsrPrim) (0x8000))

/************************************************************************************
 * Primitive definitions for Synergy framework
 ************************************************************************************/
#define CSR_SYNERGY_EVENT_CLASS_BASE        ((u16) (0x0600))

#define CSR_HCI_PRIM                        ((u16) (0x0000 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_BCCMD_PRIM                      ((u16) (0x0001 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_HQ_PRIM                         ((u16) (0x0002 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_VM_PRIM                         ((u16) (0x0003 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_TM_BLUECORE_PRIM                ((u16) (0x0004 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_FP_PRIM                         ((u16) (0x0005 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_IP_SOCKET_PRIM                  ((u16) (0x0006 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_IP_ETHER_PRIM                   ((u16) (0x0007 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_IP_IFCONFIG_PRIM                ((u16) (0x0008 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_IP_INTERNAL_PRIM                ((u16) (0x0009 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_FSAL_PRIM                       ((u16) (0x000A | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_DATA_STORE_PRIM                 ((u16) (0x000B | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_AM_PRIM                         ((u16) (0x000C | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_TLS_PRIM                        ((u16) (0x000D | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_DHCP_SERVER_PRIM                ((u16) (0x000E | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_TFTP_PRIM                       ((u16) (0x000F | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_DSPM_PRIM                       ((u16) (0x0010 | CSR_SYNERGY_EVENT_CLASS_BASE))
#define CSR_TLS_INTERNAL_PRIM               ((u16) (0x0011 | CSR_SYNERGY_EVENT_CLASS_BASE))

#define NUMBER_OF_CSR_FW_EVENTS             (CSR_DSPM_PRIM - CSR_SYNERGY_EVENT_CLASS_BASE + 1)

#define CSR_SYNERGY_EVENT_CLASS_MISC_BASE   ((u16) (0x06A0))

#define CSR_UI_PRIM                         ((u16) (0x0000 | CSR_SYNERGY_EVENT_CLASS_MISC_BASE))
#define CSR_APP_PRIM                        ((u16) (0x0001 | CSR_SYNERGY_EVENT_CLASS_MISC_BASE))
#define CSR_SDIO_PROBE_PRIM                 ((u16) (0x0002 | CSR_SYNERGY_EVENT_CLASS_MISC_BASE))

#define NUMBER_OF_CSR_FW_MISC_EVENTS        (CSR_SDIO_PROBE_PRIM - CSR_SYNERGY_EVENT_CLASS_MISC_BASE + 1)

#define CSR_ENV_PRIM                        ((u16) (0x00FF | CSR_SYNERGY_EVENT_CLASS_MISC_BASE))

#endif /* CSR_PRIM_DEFS_H__ */
