/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 * FILE: csr_wifi_hip_xbv.h
 *
 * PURPOSE:
 *      Definitions and declarations for code to read XBV files - the UniFi
 *      firmware download file format.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __XBV_H__
#define __XBV_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_XBV_TEST
/* Driver includes */
#include "csr_wifi_hip_unifi.h"
#endif


struct VMEQ
{
    u32 addr;
    u16 mask;
    u16 value;
};

struct VAND
{
    u32 first;
    u32 count;
};

struct VERS
{
    u32 num_vand;
};

struct FWDL
{
    u32 dl_addr;
    u32 dl_size;
    u32 dl_offset;
};

struct FWOV
{
    u32 dl_size;
    u32 dl_offset;
};

struct PTDL
{
    u32 dl_size;
    u32 dl_offset;
};

#define MAX_VMEQ 64
#define MAX_VAND 64
#define MAX_FWDL 256
#define MAX_PTDL 256

/* An XBV1 file can either contain firmware or patches (at the
 * moment).  The 'mode' member of the xbv1_t structure tells us which
 * one is the case. */
typedef enum
{
    xbv_unknown,
    xbv_firmware,
    xbv_patch
} xbv_mode;

typedef struct
{
    xbv_mode mode;

    /* Parts of a Firmware XBV1 */

    struct VMEQ vmeq[MAX_VMEQ];
    u32   num_vmeq;
    struct VAND vand[MAX_VAND];
    struct VERS vers;

    u32 slut_addr;

    /* F/W download image, possibly more than one part */
    struct FWDL fwdl[MAX_FWDL];
    s16    num_fwdl;

    /* F/W overlay image, add r not used */
    struct FWOV fwov;

    /* Parts of a Patch XBV1 */

    u32 build_id;

    struct PTDL ptdl[MAX_PTDL];
    s16    num_ptdl;
}  xbv1_t;


typedef s32 (*fwreadfn_t)(void *ospriv, void *dlpriv, u32 offset, void *buf, u32 len);

CsrResult xbv1_parse(card_t *card, fwreadfn_t readfn, void *dlpriv, xbv1_t *fwinfo);
s32 xbv1_read_slut(card_t *card, fwreadfn_t readfn, void *dlpriv, xbv1_t *fwinfo,
                        symbol_t *slut, u32 slut_len);
void* xbv_to_patch(card_t *card, fwreadfn_t readfn, const void *fw_buf, const xbv1_t *fwinfo,
                   u32 *size);

#ifdef __cplusplus
}
#endif

#endif /* __XBV_H__ */
