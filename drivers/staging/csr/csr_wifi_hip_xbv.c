/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 * FILE: csr_wifi_hip_xbv.c
 *
 * PURPOSE:
 *      Routines for downloading firmware to UniFi.
 *
 *      UniFi firmware files use a nested TLV (Tag-Length-Value) format.
 *
 * ---------------------------------------------------------------------------
 */

#ifdef CSR_WIFI_XBV_TEST
/* Standalone test harness */
#include "unifi_xbv.h"
#include "csr_wifi_hip_unifihw.h"
#else
/* Normal driver build */
#include "csr_wifi_hip_unifiversion.h"
#include "csr_wifi_hip_card.h"
#define DBG_TAG(t)
#endif

#include "csr_wifi_hip_xbv.h"

#define STREAM_CHECKSUM 0x6d34        /* Sum of uint16s in each patch stream */

/* XBV sizes used in patch conversion
 */
#define PTDL_MAX_SIZE 2048            /* Max bytes allowed per PTDL */
#define PTDL_HDR_SIZE (4 + 2 + 6 + 2) /* sizeof(fw_id, sec_len, patch_cmd, csum) */

/* Struct to represent a buffer for reading firmware file */

typedef struct
{
    void      *dlpriv;
    s32   ioffset;
    fwreadfn_t iread;
} ct_t;

/* Struct to represent a TLV field */
typedef struct
{
    char t_name[4];
    u32     t_len;
} tag_t;


#define TAG_EQ(i, v)    (((i)[0] == (v)[0]) &&  \
                         ((i)[1] == (v)[1]) &&  \
                         ((i)[2] == (v)[2]) &&  \
                         ((i)[3] == (v)[3]))

/* We create a small stack on the stack that contains an enum
 * indicating the containing list segments, and the offset at which
 * those lists end.  This enables a lot more error checking. */
typedef enum
{
    xbv_xbv1,
    /*xbv_info,*/
    xbv_fw,
    xbv_vers,
    xbv_vand,
    xbv_ptch,
    xbv_other
} xbv_container;

#define XBV_STACK_SIZE 6
#define XBV_MAX_OFFS   0x7fffffff

typedef struct
{
    struct
    {
        xbv_container container;
        s32      ioffset_end;
    } s[XBV_STACK_SIZE];
    u32 ptr;
} xbv_stack_t;

static s32 read_tag(card_t *card, ct_t *ct, tag_t *tag);
static s32 read_bytes(card_t *card, ct_t *ct, void *buf, u32 len);
static s32 read_uint(card_t *card, ct_t *ct, u32 *u, u32 len);
static s32 xbv_check(xbv1_t *fwinfo, const xbv_stack_t *stack,
                          xbv_mode new_mode, xbv_container old_cont);
static s32 xbv_push(xbv1_t *fwinfo, xbv_stack_t *stack,
                         xbv_mode new_mode, xbv_container old_cont,
                         xbv_container new_cont, u32 ioff);

static u32 write_uint16(void *buf, const u32 offset,
                              const u16 val);
static u32 write_uint32(void *buf, const u32 offset,
                              const u32 val);
static u32 write_bytes(void *buf, const u32 offset,
                             const u8 *data, const u32 len);
static u32 write_tag(void *buf, const u32 offset,
                           const char *tag_str);
static u32 write_chunk(void *buf, const u32 offset,
                             const char *tag_str,
                             const u32 payload_len);
static u16 calc_checksum(void *buf, const u32 offset,
                               const u32 bytes_len);
static u32 calc_patch_size(const xbv1_t *fwinfo);

static u32 write_xbv_header(void *buf, const u32 offset,
                                  const u32 file_payload_length);
static u32 write_ptch_header(void *buf, const u32 offset,
                                   const u32 fw_id);
static u32 write_patchcmd(void *buf, const u32 offset,
                                const u32 dst_genaddr, const u16 len);
static u32 write_reset_ptdl(void *buf, const u32 offset,
                                  const xbv1_t *fwinfo, u32 fw_id);
static u32 write_fwdl_to_ptdl(void *buf, const u32 offset,
                                    fwreadfn_t readfn, const struct FWDL *fwdl,
                                    const void *fw_buf, const u32 fw_id,
                                    void *rdbuf);

/*
 * ---------------------------------------------------------------------------
 *  parse_xbv1
 *
 *      Scan the firmware file to find the TLVs we are interested in.
 *      Actions performed:
 *        - check we support the file format version in VERF
 *      Store these TLVs if we have a firmware image:
 *        - SLTP Symbol Lookup Table Pointer
 *        - FWDL firmware download segments
 *        - FWOL firmware overlay segment
 *        - VMEQ Register probe tests to verify matching h/w
 *      Store these TLVs if we have a patch file:
 *        - FWID the firmware build ID that this file patches
 *        - PTDL The actual patches
 *
 *      The structure pointed to by fwinfo is cleared and
 *      'fwinfo->mode' is set to 'unknown'.  The 'fwinfo->mode'
 *      variable is set to 'firmware' or 'patch' once we know which
 *      sort of XBV file we have.
 *
 *  Arguments:
 *      readfn          Pointer to function to call to read from the file.
 *      dlpriv          Opaque pointer arg to pass to readfn.
 *      fwinfo          Pointer to fwinfo struct to fill in.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, CSR error code on failure
 * ---------------------------------------------------------------------------
 */
CsrResult xbv1_parse(card_t *card, fwreadfn_t readfn, void *dlpriv, xbv1_t *fwinfo)
{
    ct_t ct;
    tag_t tag;
    xbv_stack_t stack;

    ct.dlpriv = dlpriv;
    ct.ioffset = 0;
    ct.iread = readfn;

    memset(fwinfo, 0, sizeof(xbv1_t));
    fwinfo->mode = xbv_unknown;

    /* File must start with XBV1 triplet */
    if (read_tag(card, &ct, &tag) <= 0)
    {
        unifi_error(NULL, "File is not UniFi firmware\n");
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    DBG_TAG(tag.t_name);

    if (!TAG_EQ(tag.t_name, "XBV1"))
    {
        unifi_error(NULL, "File is not UniFi firmware (%s)\n", tag.t_name);
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    stack.ptr = 0;
    stack.s[stack.ptr].container = xbv_xbv1;
    stack.s[stack.ptr].ioffset_end = XBV_MAX_OFFS;

    /* Now scan the file */
    while (1)
    {
        s32 n;

        n = read_tag(card, &ct, &tag);
        if (n < 0)
        {
            unifi_error(NULL, "No tag\n");
            return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
        }
        if (n == 0)
        {
            /* End of file */
            break;
        }

        DBG_TAG(tag.t_name);

        /* File format version */
        if (TAG_EQ(tag.t_name, "VERF"))
        {
            u32 version;

            if (xbv_check(fwinfo, &stack, xbv_unknown, xbv_xbv1) ||
                (tag.t_len != 2) ||
                read_uint(card, &ct, &version, 2))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            if (version != 0)
            {
                unifi_error(NULL, "Unsupported firmware file version: %d.%d\n",
                            version >> 8, version & 0xFF);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
        }
        else if (TAG_EQ(tag.t_name, "LIST"))
        {
            char name[4];
            u32 list_end;

            list_end = ct.ioffset + tag.t_len;

            if (read_bytes(card, &ct, name, 4))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            DBG_TAG(name);
            if (TAG_EQ(name, "FW  "))
            {
                if (xbv_push(fwinfo, &stack, xbv_firmware, xbv_xbv1, xbv_fw, list_end))
                {
                    return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
                }
            }
            else if (TAG_EQ(name, "VERS"))
            {
                if (xbv_push(fwinfo, &stack, xbv_firmware, xbv_fw, xbv_vers, list_end) ||
                    (fwinfo->vers.num_vand != 0))
                {
                    return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
                }
            }
            else if (TAG_EQ(name, "VAND"))
            {
                struct VAND *vand;

                if (xbv_push(fwinfo, &stack, xbv_firmware, xbv_vers, xbv_vand, list_end) ||
                    (fwinfo->vers.num_vand >= MAX_VAND))
                {
                    return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
                }

                /* Get a new VAND */
                vand = fwinfo->vand + fwinfo->vers.num_vand++;

                /* Fill it in */
                vand->first = fwinfo->num_vmeq;
                vand->count = 0;
            }
            else if (TAG_EQ(name, "PTCH"))
            {
                if (xbv_push(fwinfo, &stack, xbv_patch, xbv_xbv1, xbv_ptch, list_end))
                {
                    return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
                }
            }
            else
            {
                /* Skip over any other lists.  We dont bother to push
                 * the new list type now as we would only pop it at
                 * the end of the outer loop. */
                ct.ioffset += tag.t_len - 4;
            }
        }
        else if (TAG_EQ(tag.t_name, "SLTP"))
        {
            u32 addr;

            if (xbv_check(fwinfo, &stack, xbv_firmware, xbv_fw) ||
                (tag.t_len != 4) ||
                (fwinfo->slut_addr != 0) ||
                read_uint(card, &ct, &addr, 4))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            fwinfo->slut_addr = addr;
        }
        else if (TAG_EQ(tag.t_name, "FWDL"))
        {
            u32 addr;
            struct FWDL *fwdl;

            if (xbv_check(fwinfo, &stack, xbv_firmware, xbv_fw) ||
                (fwinfo->num_fwdl >= MAX_FWDL) ||
                (read_uint(card, &ct, &addr, 4)))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            fwdl = fwinfo->fwdl + fwinfo->num_fwdl++;

            fwdl->dl_size = tag.t_len - 4;
            fwdl->dl_addr = addr;
            fwdl->dl_offset = ct.ioffset;

            ct.ioffset += tag.t_len - 4;
        }
        else if (TAG_EQ(tag.t_name, "FWOV"))
        {
            if (xbv_check(fwinfo, &stack, xbv_firmware, xbv_fw) ||
                (fwinfo->fwov.dl_size != 0) ||
                (fwinfo->fwov.dl_offset != 0))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            fwinfo->fwov.dl_size = tag.t_len;
            fwinfo->fwov.dl_offset = ct.ioffset;

            ct.ioffset += tag.t_len;
        }
        else if (TAG_EQ(tag.t_name, "VMEQ"))
        {
            u32 temp[3];
            struct VAND *vand;
            struct VMEQ *vmeq;

            if (xbv_check(fwinfo, &stack, xbv_firmware, xbv_vand) ||
                (fwinfo->num_vmeq >= MAX_VMEQ) ||
                (fwinfo->vers.num_vand == 0) ||
                (tag.t_len != 8) ||
                read_uint(card, &ct, &temp[0], 4) ||
                read_uint(card, &ct, &temp[1], 2) ||
                read_uint(card, &ct, &temp[2], 2))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            /* Get the last VAND */
            vand = fwinfo->vand + (fwinfo->vers.num_vand - 1);

            /* Get a new VMEQ */
            vmeq = fwinfo->vmeq + fwinfo->num_vmeq++;

            /* Note that this VAND contains another VMEQ */
            vand->count++;

            /* Fill in the VMEQ */
            vmeq->addr = temp[0];
            vmeq->mask = (u16)temp[1];
            vmeq->value = (u16)temp[2];
        }
        else if (TAG_EQ(tag.t_name, "FWID"))
        {
            u32 build_id;

            if (xbv_check(fwinfo, &stack, xbv_patch, xbv_ptch) ||
                (tag.t_len != 4) ||
                (fwinfo->build_id != 0) ||
                read_uint(card, &ct, &build_id, 4))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            fwinfo->build_id = build_id;
        }
        else if (TAG_EQ(tag.t_name, "PTDL"))
        {
            struct PTDL *ptdl;

            if (xbv_check(fwinfo, &stack, xbv_patch, xbv_ptch) ||
                (fwinfo->num_ptdl >= MAX_PTDL))
            {
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }

            /* Allocate a new PTDL */
            ptdl = fwinfo->ptdl + fwinfo->num_ptdl++;

            ptdl->dl_size = tag.t_len;
            ptdl->dl_offset = ct.ioffset;

            ct.ioffset += tag.t_len;
        }
        else
        {
            /*
             * If we get here it is a tag we are not interested in,
             * just skip over it.
             */
            ct.ioffset += tag.t_len;
        }

        /* Check to see if we are at the end of the currently stacked
         * segment.  We could finish more than one list at a time. */
        while (ct.ioffset >= stack.s[stack.ptr].ioffset_end)
        {
            if (ct.ioffset > stack.s[stack.ptr].ioffset_end)
            {
                unifi_error(NULL,
                            "XBV file has overrun stack'd segment %d (%d > %d)\n",
                            stack.ptr, ct.ioffset, stack.s[stack.ptr].ioffset_end);
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            if (stack.ptr <= 0)
            {
                unifi_error(NULL, "XBV file has underrun stack pointer\n");
                return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            }
            stack.ptr--;
        }
    }

    if (stack.ptr != 0)
    {
        unifi_error(NULL, "Last list of XBV is not complete.\n");
        return CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }

    return CSR_RESULT_SUCCESS;
} /* xbv1_parse() */


/* Check the the XBV file is of a consistant sort (either firmware or
 * patch) and that we are in the correct containing list type. */
static s32 xbv_check(xbv1_t *fwinfo, const xbv_stack_t *stack,
                          xbv_mode new_mode, xbv_container old_cont)
{
    /* If the new file mode is unknown the current packet could be in
     * either (any) type of XBV file, and we cant make a decission at
     * this time. */
    if (new_mode != xbv_unknown)
    {
        if (fwinfo->mode == xbv_unknown)
        {
            fwinfo->mode = new_mode;
        }
        else if (fwinfo->mode != new_mode)
        {
            return -1;
        }
    }
    /* If the current stack top doesn't match what we expect then the
     * file is corrupt. */
    if (stack->s[stack->ptr].container != old_cont)
    {
        return -1;
    }
    return 0;
}


/* Make checks as above and then enter a new list */
static s32 xbv_push(xbv1_t *fwinfo, xbv_stack_t *stack,
                         xbv_mode new_mode, xbv_container old_cont,
                         xbv_container new_cont, u32 new_ioff)
{
    if (xbv_check(fwinfo, stack, new_mode, old_cont))
    {
        return -1;
    }

    /* Check that our stack won't overflow. */
    if (stack->ptr >= (XBV_STACK_SIZE - 1))
    {
        return -1;
    }

    /* Add the new list element to the top of the stack. */
    stack->ptr++;
    stack->s[stack->ptr].container = new_cont;
    stack->s[stack->ptr].ioffset_end = new_ioff;

    return 0;
}


static u32 xbv2uint(u8 *ptr, s32 len)
{
    u32 u = 0;
    s16 i;

    for (i = 0; i < len; i++)
    {
        u32 b;
        b = ptr[i];
        u += b << (i * 8);
    }
    return u;
}


static s32 read_tag(card_t *card, ct_t *ct, tag_t *tag)
{
    u8 buf[8];
    s32 n;

    n = (*ct->iread)(card->ospriv, ct->dlpriv, ct->ioffset, buf, 8);
    if (n <= 0)
    {
        return n;
    }

    /* read the tag and length */
    if (n != 8)
    {
        return -1;
    }

    /* get section tag */
    CsrMemCpy(tag->t_name, buf, 4);

    /* get section length */
    tag->t_len = xbv2uint(buf + 4, 4);

    ct->ioffset += 8;

    return 8;
} /* read_tag() */


static s32 read_bytes(card_t *card, ct_t *ct, void *buf, u32 len)
{
    /* read the tag value */
    if ((*ct->iread)(card->ospriv, ct->dlpriv, ct->ioffset, buf, len) != (s32)len)
    {
        return -1;
    }

    ct->ioffset += len;

    return 0;
} /* read_bytes() */


static s32 read_uint(card_t *card, ct_t *ct, u32 *u, u32 len)
{
    u8 buf[4];

    /* Integer cannot be more than 4 bytes */
    if (len > 4)
    {
        return -1;
    }

    if (read_bytes(card, ct, buf, len))
    {
        return -1;
    }

    *u = xbv2uint(buf, len);

    return 0;
} /* read_uint() */


static u32 write_uint16(void *buf, const u32 offset, const u16 val)
{
    u8 *dst = (u8 *)buf + offset;
    *dst++ = (u8)(val & 0xff); /* LSB first */
    *dst = (u8)(val >> 8);
    return sizeof(u16);
}


static u32 write_uint32(void *buf, const u32 offset, const u32 val)
{
    (void)write_uint16(buf, offset + 0, (u16)(val & 0xffff));
    (void)write_uint16(buf, offset + 2, (u16)(val >> 16));
    return sizeof(u32);
}


static u32 write_bytes(void *buf, const u32 offset, const u8 *data, const u32 len)
{
    u32 i;
    u8 *dst = (u8 *)buf + offset;

    for (i = 0; i < len; i++)
    {
        *dst++ = *((u8 *)data + i);
    }
    return len;
}


static u32 write_tag(void *buf, const u32 offset, const char *tag_str)
{
    u8 *dst = (u8 *)buf + offset;
    CsrMemCpy(dst, tag_str, 4);
    return 4;
}


static u32 write_chunk(void *buf, const u32 offset, const char *tag_str, const u32 payload_len)
{
    u32 written = 0;
    written += write_tag(buf, offset, tag_str);
    written += write_uint32(buf, written + offset, (u32)payload_len);

    return written;
}


static u16 calc_checksum(void *buf, const u32 offset, const u32 bytes_len)
{
    u32 i;
    u8 *src = (u8 *)buf + offset;
    u16 sum = 0;
    u16 val;

    for (i = 0; i < bytes_len / 2; i++)
    {
        /* Contents copied to file is LE, host might not be */
        val = (u16) * src++;         /* LSB */
        val += (u16)(*src++) << 8;   /* MSB */
        sum += val;
    }

    /* Total of uint16s in the stream plus the stored check value
     * should equal STREAM_CHECKSUM when decoded.
     */
    return (STREAM_CHECKSUM - sum);
}


#define PTDL_RESET_DATA_SIZE  20  /* Size of reset vectors PTDL */

static u32 calc_patch_size(const xbv1_t *fwinfo)
{
    s16 i;
    u32 size = 0;

    /*
     * Work out how big an equivalent patch format file must be for this image.
     * This only needs to be approximate, so long as it's large enough.
     */
    if (fwinfo->mode != xbv_firmware)
    {
        return 0;
    }

    /* Payload (which will get put into a series of PTDLs) */
    for (i = 0; i < fwinfo->num_fwdl; i++)
    {
        size += fwinfo->fwdl[i].dl_size;
    }

    /* Another PTDL at the end containing reset vectors */
    size += PTDL_RESET_DATA_SIZE;

    /* PTDL headers. Add one for remainder, one for reset vectors */
    size += ((fwinfo->num_fwdl / PTDL_MAX_SIZE) + 2) * PTDL_HDR_SIZE;

    /* Another 1K sufficient to cover miscellaneous headers */
    size += 1024;

    return size;
}


static u32 write_xbv_header(void *buf, const u32 offset, const u32 file_payload_length)
{
    u32 written = 0;

    /* The length value given to the XBV chunk is the length of all subsequent
     * contents of the file, excluding the 8 byte size of the XBV1 header itself
     * (The added 6 bytes thus accounts for the size of the VERF)
     */
    written += write_chunk(buf, offset + written, (char *)"XBV1", file_payload_length + 6);

    written += write_chunk(buf, offset + written, (char *)"VERF", 2);
    written += write_uint16(buf,  offset + written, 0);      /* File version */

    return written;
}


static u32 write_ptch_header(void *buf, const u32 offset, const u32 fw_id)
{
    u32 written = 0;

    /* LIST is written with a zero length, to be updated later */
    written += write_chunk(buf, offset + written, (char *)"LIST", 0);
    written += write_tag(buf, offset + written, (char *)"PTCH");        /* List type */

    written += write_chunk(buf, offset + written, (char *)"FWID", 4);
    written += write_uint32(buf, offset + written, fw_id);


    return written;
}


#define UF_REGION_PHY  1
#define UF_REGION_MAC  2
#define UF_MEMPUT_MAC  0x0000
#define UF_MEMPUT_PHY  0x1000

static u32 write_patchcmd(void *buf, const u32 offset, const u32 dst_genaddr, const u16 len)
{
    u32 written = 0;
    u32 region = (dst_genaddr >> 28);
    u16 cmd_and_len = UF_MEMPUT_MAC;

    if (region == UF_REGION_PHY)
    {
        cmd_and_len = UF_MEMPUT_PHY;
    }
    else if (region != UF_REGION_MAC)
    {
        return 0; /* invalid */
    }

    /* Write the command and data length */
    cmd_and_len |= len;
    written += write_uint16(buf, offset + written, cmd_and_len);

    /* Write the destination generic address */
    written += write_uint16(buf, offset + written, (u16)(dst_genaddr >> 16));
    written += write_uint16(buf, offset + written, (u16)(dst_genaddr & 0xffff));

    /* The data payload should be appended to the command */
    return written;
}


static u32 write_fwdl_to_ptdl(void *buf, const u32 offset, fwreadfn_t readfn,
                                    const struct FWDL *fwdl, const void *dlpriv,
                                    const u32 fw_id, void *fw_buf)
{
    u32 written = 0;
    s16 chunks = 0;
    u32 left = fwdl->dl_size;      /* Bytes left in this fwdl */
    u32 dl_addr = fwdl->dl_addr;   /* Target address of fwdl image on XAP */
    u32 dl_offs = fwdl->dl_offset; /* Offset of fwdl image data in source */
    u16 csum;
    u32 csum_start_offs;           /* first offset to include in checksum */
    u32 sec_data_len;              /* section data byte count */
    u32 sec_len;                   /* section data + header byte count */

    /* FWDL maps to one or more PTDLs, as max size for a PTDL is 1K words */
    while (left)
    {
        /* Calculate amount to be transferred */
        sec_data_len = CSRMIN(left, PTDL_MAX_SIZE - PTDL_HDR_SIZE);
        sec_len = sec_data_len + PTDL_HDR_SIZE;

        /* Write PTDL header + entire PTDL size */
        written += write_chunk(buf, offset + written, (char *)"PTDL", sec_len);
        /* bug digest implies 4 bytes of padding here, but that seems wrong */

        /* Checksum starts here */
        csum_start_offs = offset + written;

        /* Patch-chunk header: fw_id. Note that this is in XAP word order */
        written += write_uint16(buf, offset + written, (u16)(fw_id >> 16));
        written += write_uint16(buf, offset + written, (u16)(fw_id & 0xffff));

        /* Patch-chunk header: section length in uint16s */
        written += write_uint16(buf, offset + written, (u16)(sec_len / 2));


        /* Write the appropriate patch command for the data's destination ptr */
        written += write_patchcmd(buf, offset + written, dl_addr, (u16)(sec_data_len / 2));

        /* Write the data itself (limited to the max chunk length) */
        if (readfn(NULL, (void *)dlpriv, dl_offs, fw_buf, sec_data_len) < 0)
        {
            return 0;
        }

        written += write_bytes(buf,
                               offset + written,
                               fw_buf,
                               sec_data_len);

        /* u16 checksum calculated over data written */
        csum = calc_checksum(buf, csum_start_offs, written - (csum_start_offs - offset));
        written += write_uint16(buf, offset + written, csum);

        left -= sec_data_len;
        dl_addr += sec_data_len;
        dl_offs += sec_data_len;
        chunks++;
    }

    return written;
}


#define SEC_CMD_LEN         ((4 + 2) * 2) /* sizeof(cmd, vector) per XAP */
#define PTDL_VEC_HDR_SIZE   (4 + 2 + 2)   /* sizeof(fw_id, sec_len, csum) */
#define UF_MAC_START_VEC    0x00c00000    /* Start address of image on MAC */
#define UF_PHY_START_VEC    0x00c00000    /* Start address of image on PHY */
#define UF_MAC_START_CMD    0x6000        /* MAC "Set start address" command */
#define UF_PHY_START_CMD    0x7000        /* PHY "Set start address" command */

static u32 write_reset_ptdl(void *buf, const u32 offset, const xbv1_t *fwinfo, u32 fw_id)
{
    u32 written = 0;
    u16 csum;
    u32 csum_start_offs;                 /* first offset to include in checksum */
    u32 sec_len;                         /* section data + header byte count */

    sec_len = SEC_CMD_LEN + PTDL_VEC_HDR_SIZE; /* Total section byte length */

    /* Write PTDL header + entire PTDL size */
    written += write_chunk(buf, offset + written, (char *)"PTDL", sec_len);

    /* Checksum starts here */
    csum_start_offs = offset + written;

    /* Patch-chunk header: fw_id. Note that this is in XAP word order */
    written += write_uint16(buf, offset + written, (u16)(fw_id >> 16));
    written += write_uint16(buf, offset + written, (u16)(fw_id & 0xffff));

    /* Patch-chunk header: section length in uint16s */
    written += write_uint16(buf, offset + written, (u16)(sec_len / 2));

    /*
     * Restart addresses to be executed on subsequent loader restart command.
     */

    /* Setup the MAC start address, note word ordering */
    written += write_uint16(buf, offset + written, UF_MAC_START_CMD);
    written += write_uint16(buf, offset + written, (UF_MAC_START_VEC >> 16));
    written += write_uint16(buf, offset + written, (UF_MAC_START_VEC & 0xffff));

    /* Setup the PHY start address, note word ordering */
    written += write_uint16(buf, offset + written, UF_PHY_START_CMD);
    written += write_uint16(buf, offset + written, (UF_PHY_START_VEC >> 16));
    written += write_uint16(buf, offset + written, (UF_PHY_START_VEC & 0xffff));

    /* u16 checksum calculated over data written */
    csum = calc_checksum(buf, csum_start_offs, written - (csum_start_offs - offset));
    written += write_uint16(buf, offset + written, csum);

    return written;
}


/*
 * ---------------------------------------------------------------------------
 *  read_slut
 *
 *      desc
 *
 *  Arguments:
 *      readfn          Pointer to function to call to read from the file.
 *      dlpriv          Opaque pointer arg to pass to readfn.
 *      addr            Offset into firmware image of SLUT.
 *      fwinfo          Pointer to fwinfo struct to fill in.
 *
 *  Returns:
 *      Number of SLUT entries in the f/w, or -1 if the image was corrupt.
 * ---------------------------------------------------------------------------
 */
s32 xbv1_read_slut(card_t *card, fwreadfn_t readfn, void *dlpriv, xbv1_t *fwinfo,
                        symbol_t *slut, u32 slut_len)
{
    s16 i;
    s32 offset;
    u32 magic;
    u32 count = 0;
    ct_t ct;

    if (fwinfo->mode != xbv_firmware)
    {
        return -1;
    }

    /* Find the d/l segment containing the SLUT */
    /* This relies on the SLUT being entirely contained in one segment */
    offset = -1;
    for (i = 0; i < fwinfo->num_fwdl; i++)
    {
        if ((fwinfo->slut_addr >= fwinfo->fwdl[i].dl_addr) &&
            (fwinfo->slut_addr < (fwinfo->fwdl[i].dl_addr + fwinfo->fwdl[i].dl_size)))
        {
            offset = fwinfo->fwdl[i].dl_offset +
                     (fwinfo->slut_addr - fwinfo->fwdl[i].dl_addr);
        }
    }
    if (offset < 0)
    {
        return -1;
    }

    ct.dlpriv = dlpriv;
    ct.ioffset = offset;
    ct.iread = readfn;

    if (read_uint(card, &ct, &magic, 2))
    {
        return -1;
    }
    if (magic != SLUT_FINGERPRINT)
    {
        return -1;
    }

    while (count < slut_len)
    {
        u32 id, obj;

        /* Read Symbol Id */
        if (read_uint(card, &ct, &id, 2))
        {
            return -1;
        }

        /* Check for end of table marker */
        if (id == CSR_SLT_END)
        {
            break;
        }

        /* Read Symbol Value */
        if (read_uint(card, &ct, &obj, 4))
        {
            return -1;
        }

        slut[count].id  = (u16)id;
        slut[count].obj = obj;
        count++;
    }

    return count;
} /* read_slut() */


/*
 * ---------------------------------------------------------------------------
 *  xbv_to_patch
 *
 *      Convert (the relevant parts of) a firmware xbv file into a patch xbv
 *
 *  Arguments:
 *      card
 *      fw_buf - pointer to xbv firmware image
 *      fwinfo - structure describing the firmware image
 *      size   - pointer to location into which size of f/w is written.
 *
 *  Returns:
 *      Pointer to firmware image, or NULL on error. Caller must free this
 *      buffer via CsrMemFree() once it's finished with.
 *
 *  Notes:
 *      The input fw_buf should have been checked via xbv1_parse prior to
 *      calling this function, so the input image is assumed valid.
 * ---------------------------------------------------------------------------
 */
#define PTCH_LIST_SIZE 16         /* sizeof PTCH+FWID chunk in LIST header */

void* xbv_to_patch(card_t *card, fwreadfn_t readfn,
                   const void *fw_buf, const xbv1_t *fwinfo, u32 *size)
{
    void *patch_buf = NULL;
    u32 patch_buf_size;
    u32 payload_offs = 0;           /* Start of XBV payload */
    s16 i;
    u32 patch_offs = 0;
    u32 list_len_offs = 0;          /* Offset of PTDL LIST length parameter */
    u32 ptdl_start_offs = 0;        /* Offset of first PTDL chunk */
    u32 fw_id;
    void *rdbuf;

    if (!fw_buf || !fwinfo || !card)
    {
        return NULL;
    }

    if (fwinfo->mode != xbv_firmware)
    {
        unifi_error(NULL, "Not a firmware file\n");
        return NULL;
    }

    /* Pre-allocate read buffer for chunk conversion */
    rdbuf = CsrMemAlloc(PTDL_MAX_SIZE);
    if (!rdbuf)
    {
        unifi_error(card, "Couldn't alloc conversion buffer\n");
        return NULL;
    }

    /* Loader requires patch file's build ID to match the running firmware's */
    fw_id = card->build_id;

    /* Firmware XBV1 contains VERF, optional INFO, SLUT(s), FWDL(s)          */
    /* Other chunks should get skipped.                                      */
    /* VERF should be sanity-checked against chip version                    */

    /* Patch    XBV1 contains VERF, optional INFO, PTCH                      */
    /*          PTCH contains FWID, optional INFO, PTDL(s), PTDL(start_vec)  */
    /* Each FWDL is split into PTDLs (each is 1024 XAP words max)            */
    /* Each PTDL contains running ROM f/w version, and checksum              */
    /* MAC/PHY reset addresses (known) are added into a final PTDL           */

    /* The input image has already been parsed, and loaded into fwinfo, so we
     * can use that to build the output image
     */
    patch_buf_size = calc_patch_size(fwinfo);

    patch_buf = (void *)CsrMemAlloc(patch_buf_size);
    if (!patch_buf)
    {
        CsrMemFree(rdbuf);
        unifi_error(NULL, "Can't malloc buffer for patch conversion\n");
        return NULL;
    }

    memset(patch_buf, 0xdd, patch_buf_size);

    /* Write XBV + VERF headers */
    patch_offs += write_xbv_header(patch_buf, patch_offs, 0);
    payload_offs = patch_offs;

    /* Write patch (LIST) header */
    list_len_offs = patch_offs + 4;    /* Save LIST.length offset for later update */
    patch_offs += write_ptch_header(patch_buf, patch_offs, fw_id);

    /* Save start offset of the PTDL chunks */
    ptdl_start_offs = patch_offs;

    /* Write LIST of firmware PTDL blocks */
    for (i = 0; i < fwinfo->num_fwdl; i++)
    {
        patch_offs += write_fwdl_to_ptdl(patch_buf,
                                         patch_offs,
                                         readfn,
                                         &fwinfo->fwdl[i],
                                         fw_buf,
                                         fw_id,
                                         rdbuf);
    }

    /* Write restart-vector PTDL last */
    patch_offs += write_reset_ptdl(patch_buf, patch_offs, fwinfo, fw_id);

    /* Now the length is known, update the LIST.length */
    (void)write_uint32(patch_buf, list_len_offs,
                       (patch_offs - ptdl_start_offs) + PTCH_LIST_SIZE);

    /* Re write XBV headers just to fill in the correct file size */
    (void)write_xbv_header(patch_buf, 0, (patch_offs - payload_offs));

    unifi_trace(card->ospriv, UDBG1, "XBV:PTCH size %u, fw_id %u\n",
                patch_offs, fw_id);
    if (size)
    {
        *size = patch_offs;
    }
    CsrMemFree(rdbuf);

    return patch_buf;
}


