/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/*
 * ---------------------------------------------------------------------------
 * FILE: csr_wifi_hip_dump.c
 *
 * PURPOSE:
 *      Routines for retrieving and buffering core status from the UniFi
 *
 * ---------------------------------------------------------------------------
 */
#include <linux/slab.h>
#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_unifiversion.h"
#include "csr_wifi_hip_card.h"

/* Locations to capture in dump (XAP words) */
#define HIP_CDUMP_FIRST_CPUREG      (0xFFE0) /* First CPU register */
#define HIP_CDUMP_FIRST_LO          (0)      /* Start of low address range */
#define HIP_CDUMP_FIRST_HI_MAC      (0x3C00) /* Start of MAC high area */
#define HIP_CDUMP_FIRST_HI_PHY      (0x1C00) /* Start of PHY high area */
#define HIP_CDUMP_FIRST_SH          (0)      /* Start of shared memory area */

#define HIP_CDUMP_NCPUREGS    (10)           /* No. of 16-bit XAP registers */
#define HIP_CDUMP_NWORDS_LO   (0x0100)       /* Low area size in 16-bit words */
#define HIP_CDUMP_NWORDS_HI   (0x0400)       /* High area size in 16-bit words */
#define HIP_CDUMP_NWORDS_SH   (0x0500)       /* Shared memory area size, 16-bit words */

#define HIP_CDUMP_NUM_ZONES 7                /* Number of UniFi memory areas to capture */

/* Mini-coredump state */
typedef struct coredump_buf
{
    u16  count;                       /* serial number of dump */
    CsrTime    timestamp;                   /* host's system time at capture */
    s16   requestor;                   /* request: 0=auto dump, 1=manual */
    u16  chip_ver;
    u32  fw_ver;
    u16 *zone[HIP_CDUMP_NUM_ZONES];

    struct coredump_buf *next;              /* circular list */
    struct coredump_buf *prev;              /* circular list */
} coredump_buffer;

/* Structure used to describe a zone of chip memory captured by mini-coredump */
struct coredump_zone
{
    unifi_coredump_space_t           space;  /* XAP memory space this zone covers */
    enum unifi_dbg_processors_select cpu;    /* XAP CPU core selector */
    u32                        gp;     /* Generic Pointer to memory zone on XAP */
    u16                        offset; /* 16-bit XAP word offset of zone in memory space */
    u16                        length; /* Length of zone in XAP words */
};

static CsrResult unifi_coredump_from_sdio(card_t *card, coredump_buffer *dump_buf);
static CsrResult unifi_coredump_read_zones(card_t *card, coredump_buffer *dump_buf);
static CsrResult unifi_coredump_read_zone(card_t *card, u16 *zone,
                                          const struct coredump_zone *def);
static s32 get_value_from_coredump(const coredump_buffer *dump,
                                        const unifi_coredump_space_t space, const u16 offset);

/* Table of chip memory zones we capture on mini-coredump */
static const struct coredump_zone zonedef_table[HIP_CDUMP_NUM_ZONES] = {
    { UNIFI_COREDUMP_MAC_REG,  UNIFI_PROC_MAC, UNIFI_MAKE_GP(REGISTERS, HIP_CDUMP_FIRST_CPUREG * 2), HIP_CDUMP_FIRST_CPUREG, HIP_CDUMP_NCPUREGS },
    { UNIFI_COREDUMP_PHY_REG,  UNIFI_PROC_PHY, UNIFI_MAKE_GP(REGISTERS, HIP_CDUMP_FIRST_CPUREG * 2), HIP_CDUMP_FIRST_CPUREG, HIP_CDUMP_NCPUREGS },
    { UNIFI_COREDUMP_SH_DMEM,  UNIFI_PROC_INVALID, UNIFI_MAKE_GP(SH_DMEM, HIP_CDUMP_FIRST_SH * 2),   HIP_CDUMP_FIRST_SH,     HIP_CDUMP_NWORDS_SH },
    { UNIFI_COREDUMP_MAC_DMEM, UNIFI_PROC_MAC, UNIFI_MAKE_GP(MAC_DMEM, HIP_CDUMP_FIRST_LO * 2),      HIP_CDUMP_FIRST_LO,     HIP_CDUMP_NWORDS_LO },
    { UNIFI_COREDUMP_MAC_DMEM, UNIFI_PROC_MAC, UNIFI_MAKE_GP(MAC_DMEM, HIP_CDUMP_FIRST_HI_MAC * 2),  HIP_CDUMP_FIRST_HI_MAC, HIP_CDUMP_NWORDS_HI },
    { UNIFI_COREDUMP_PHY_DMEM, UNIFI_PROC_PHY, UNIFI_MAKE_GP(PHY_DMEM, HIP_CDUMP_FIRST_LO * 2),      HIP_CDUMP_FIRST_LO,     HIP_CDUMP_NWORDS_LO },
    { UNIFI_COREDUMP_PHY_DMEM, UNIFI_PROC_PHY, UNIFI_MAKE_GP(PHY_DMEM, HIP_CDUMP_FIRST_HI_PHY * 2),  HIP_CDUMP_FIRST_HI_PHY, HIP_CDUMP_NWORDS_HI },
};

/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_request_at_next_reset
 *
 *      Request that a mini-coredump is performed when the driver has
 *      completed resetting the UniFi device.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      enable          If non-zero, sets the request.
 *                      If zero, cancels any pending request.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS or CSR HIP error code
 *
 *  Notes:
 *      This function is typically called once the driver has detected that
 *      the UniFi device has become unresponsive due to crash, or internal
 *      watchdog reset. The driver must reset it to regain communication and,
 *      immediately after that, the mini-coredump can be captured.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_coredump_request_at_next_reset(card_t *card, s8 enable)
{
    CsrResult r;

    func_enter();

    if (enable)
    {
        unifi_trace(card->ospriv, UDBG2, "Mini-coredump requested after reset\n");
    }

    if (card == NULL)
    {
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }
    else
    {
        card->request_coredump_on_reset = enable?1 : 0;
        r = CSR_RESULT_SUCCESS;
    }

    func_exit_r(r);
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_handle_request
 *
 *      Performs a coredump now, if one was requested, and clears the request.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS or CSR HIP error code
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_coredump_handle_request(card_t *card)
{
    CsrResult r = CSR_RESULT_SUCCESS;

    func_enter();

    if (card == NULL)
    {
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
    }
    else
    {
        if (card->request_coredump_on_reset == 1)
        {
            card->request_coredump_on_reset = 0;
            r = unifi_coredump_capture(card, NULL);
        }
    }

    func_exit_r(r);
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_capture
 *
 *      Capture the current status of the UniFi device.
 *      Various registers are buffered for future offline inspection.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      req             Pointer to request struct, or NULL:
 *                          A coredump requested manually by the user app
 *                          will have a request struct pointer, an automatic
 *                          coredump will have a NULL pointer.
 *  Returns:
 *      CSR_RESULT_SUCCESS  on success,
 *      CSR_RESULT_FAILURE  SDIO error
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE  Initialisation not complete
 *
 *  Notes:
 *      The result is a filled entry in the circular buffer of core dumps,
 *      values from which can be extracted to userland via an ioctl.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_coredump_capture(card_t *card, struct unifi_coredump_req *req)
{
    CsrResult r = CSR_RESULT_SUCCESS;
    static u16 dump_seq_no = 1;
    CsrTime time_of_capture;

    func_enter();

    if (card->dump_next_write == NULL)
    {
        r = CSR_RESULT_SUCCESS;
        goto done;
    }

    /* Reject forced capture before initialisation has happened */
    if (card->helper == NULL)
    {
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
        goto done;
    }


    /*
     * Force a mini-coredump capture right now
     */
    time_of_capture = CsrTimeGet(NULL);
    unifi_info(card->ospriv, "Mini-coredump capture at t=%u\n", time_of_capture);

    /* Wake up the processors so we can talk to them */
    r = unifi_set_host_state(card, UNIFI_HOST_STATE_AWAKE);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to wake UniFi\n");
        goto done;
    }
    CsrThreadSleep(20);

    /* Stop both XAPs */
    unifi_trace(card->ospriv, UDBG4, "Stopping XAPs for coredump capture\n");
    r = unifi_card_stop_processor(card, UNIFI_PROC_BOTH);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to stop UniFi XAPs\n");
        goto done;
    }

    /* Dump core into the next available slot in the circular list */
    r = unifi_coredump_from_sdio(card, card->dump_next_write);
    if (r == CSR_RESULT_SUCCESS)
    {
        /* Record whether the dump was manual or automatic */
        card->dump_next_write->requestor = (req?1 : 0);
        card->dump_next_write->timestamp = time_of_capture;
        /* Advance to the next buffer */
        card->dump_next_write->count = dump_seq_no++;
        card->dump_cur_read = card->dump_next_write;
        card->dump_next_write = card->dump_next_write->next;

        /* Sequence no. of zero indicates slot not in use, so handle wrap */
        if (dump_seq_no == 0)
        {
            dump_seq_no = 1;
        }

        unifi_trace(card->ospriv, UDBG3,
                    "Coredump (%p), SeqNo=%d, cur_read=%p, next_write=%p\n",
                    req,
                    card->dump_cur_read->count,
                    card->dump_cur_read, card->dump_next_write);
    }

    /* Start both XAPs */
    unifi_trace(card->ospriv, UDBG4, "Restart XAPs after coredump\n");
    r = card_start_processor(card, UNIFI_PROC_BOTH);
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Failed to start UniFi XAPs\n");
        goto done;
    }

done:
    func_exit_r(r);
    return r;
} /* unifi_coredump_capture() */


/*
 * ---------------------------------------------------------------------------
 *  get_value_from_coredump
 *
 *
 *
 *  Arguments:
 *      dump                Pointer to buffered coredump data
 *      offset_in_space     XAP memory space to retrieve from the buffer (there
 *                          may be more than one zone covering the same memory
 *                          space, but starting from different offsets).
 *      offset              Offset within the XAP memory space to be retrieved
 *
 *  Returns:
 *      >=0                  Register value on success
 *      <0                   Register out of range of any captured zones
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
static s32 get_value_from_coredump(const coredump_buffer       *coreDump,
                                        const unifi_coredump_space_t space,
                                        const u16              offset_in_space)
{
    s32 r = -1;
    u16 offset_in_zone;
    u32 zone_end_offset;
    s32 i;
    const struct coredump_zone *def = &zonedef_table[0];

    /* Search zone def table for a match with the requested memory space */
    for (i = 0; i < HIP_CDUMP_NUM_ZONES; i++, def++)
    {
        if (space == def->space)
        {
            zone_end_offset = def->offset + def->length;

            /* Is the space offset contained in this zone? */
            if (offset_in_space < zone_end_offset &&
                offset_in_space >= def->offset)
            {
                /* Calculate the offset of data within the zone buffer */
                offset_in_zone = offset_in_space - def->offset;
                r = (s32) * (coreDump->zone[i] + offset_in_zone);

                unifi_trace(NULL, UDBG6,
                            "sp %d, offs 0x%04x = 0x%04x (in z%d 0x%04x->0x%04x)\n",
                            space, offset_in_space, r,
                            i, def->offset, zone_end_offset - 1);
                break;
            }
        }
    }
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_get_value
 *
 *      Retrieve the value of a register buffered from a previous core dump,
 *      so that it may be reported back to application code.
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      req_reg         Pointer to request parameter partially filled. This
 *                      function puts in the values retrieved from the dump.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS on success, or:
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         Null parameter error
 *      CSR_WIFI_HIP_RESULT_RANGE                 Register out of range
 *      CSR_WIFI_HIP_RESULT_NOT_FOUND             Dump index not (yet) captured
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_coredump_get_value(card_t *card, struct unifi_coredump_req *req)
{
    CsrResult r;
    s32 i = 0;
    coredump_buffer *find_dump = NULL;

    func_enter();

    if (req == NULL || card == NULL)
    {
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
        goto done;
    }
    req->value = -1;
    if (card->dump_buf == NULL)
    {
        unifi_trace(card->ospriv, UDBG2, "No coredump buffers\n");
        r = CSR_WIFI_HIP_RESULT_NOT_FOUND;     /* Coredumping disabled */
        goto done;
    }
    if (card->dump_cur_read == NULL)
    {
        unifi_trace(card->ospriv, UDBG4, "No coredumps captured\n");
        r = CSR_WIFI_HIP_RESULT_NOT_FOUND;     /* No coredump yet captured */
        goto done;
    }

    /* Find the requested dump buffer */
    switch (req->index)
    {
        case 0:     /* Newest */
            find_dump = card->dump_cur_read;
            break;
        case -1:    /* Oldest: The next used slot forward */
            for (find_dump = card->dump_cur_read->next;
                 (find_dump->count == 0) && (find_dump != card->dump_cur_read);
                 find_dump = card->dump_cur_read->next)
            {
            }
            break;
        default:    /* Number of steps back from current read position */
            for (i = 0, find_dump = card->dump_cur_read;
                 i < req->index;
                 i++, find_dump = find_dump->prev)
            {
                /* Walk the list for the index'th entry, but
                 * stop when about to wrap. */
                unifi_trace(card->ospriv, UDBG6,
                            "%d: %d, @%p, p=%p, n=%p, cr=%p, h=%p\n",
                            i, find_dump->count, find_dump, find_dump->prev,
                            find_dump->next, card->dump_cur_read, card->dump_buf);
                if (find_dump->prev == card->dump_cur_read)
                {
                    /* Wrapped but still not found, index out of range */
                    if (i != req->index)
                    {
                        unifi_trace(card->ospriv, UDBG6,
                                    "Dump index %d not found %d\n", req->index, i);
                        r = CSR_WIFI_HIP_RESULT_NOT_FOUND;
                        goto done;
                    }
                    break;
                }
            }
            break;
    }

    /* Check if the slot is actually filled with a core dump */
    if (find_dump->count == 0)
    {
        unifi_trace(card->ospriv, UDBG4, "Not captured %d\n", req->index);
        r = CSR_WIFI_HIP_RESULT_NOT_FOUND;
        goto done;
    }

    unifi_trace(card->ospriv, UDBG6, "Req index %d, found seq %d at step %d\n",
                req->index, find_dump->count, i);

    /* Find the appropriate entry in the buffer */
    req->value = get_value_from_coredump(find_dump, req->space, (u16)req->offset);
    if (req->value < 0)
    {
        r = CSR_WIFI_HIP_RESULT_RANGE;     /* Un-captured register */
        unifi_trace(card->ospriv, UDBG4,
                    "Can't read space %d, reg 0x%x from coredump buffer %d\n",
                    req->space, req->offset, req->index);
    }
    else
    {
        r = CSR_RESULT_SUCCESS;
    }

    /* Update the private request structure with the found values */
    req->chip_ver = find_dump->chip_ver;
    req->fw_ver = find_dump->fw_ver;
    req->timestamp = find_dump->timestamp;
    req->requestor = find_dump->requestor;
    req->serial = find_dump->count;

done:
    func_exit_r(r);
    return r;
} /* unifi_coredump_get_value() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_read_zone
 *
 *      Captures a UniFi memory zone into a buffer on the host
 *
 *  Arguments:
 *      card          Pointer to card struct
 *      zonebuf       Pointer to on-host buffer to dump the memory zone into
 *      def           Pointer to description of the memory zone to read from UniFi.
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS                   on success, or:
 *      CSR_RESULT_FAILURE                   SDIO error
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         Parameter error
 *
 *  Notes:
 *      It is assumed that the caller has already stopped the XAPs
 * ---------------------------------------------------------------------------
 */
static CsrResult unifi_coredump_read_zone(card_t *card, u16 *zonebuf, const struct coredump_zone *def)
{
    CsrResult r;

    func_enter();

    if (zonebuf == NULL || def == NULL)
    {
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
        goto done;
    }

    /* Select XAP CPU if necessary */
    if (def->cpu != UNIFI_PROC_INVALID)
    {
        if (def->cpu != UNIFI_PROC_MAC && def->cpu != UNIFI_PROC_PHY)
        {
            r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
            goto done;
        }
        r = unifi_set_proc_select(card, def->cpu);
        if (r != CSR_RESULT_SUCCESS)
        {
            goto done;
        }
    }

    unifi_trace(card->ospriv, UDBG4,
                "Dump sp %d, offs 0x%04x, 0x%04x words @GP=%08x CPU %d\n",
                def->space, def->offset, def->length, def->gp, def->cpu);

    /* Read on-chip RAM (byte-wise) */
    r = unifi_card_readn(card, def->gp, zonebuf, (u16)(def->length * 2));
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        goto done;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Can't read UniFi shared data area\n");
        goto done;
    }

done:
    func_exit_r(r);
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_read_zones
 *
 *      Walks through the table of on-chip memory zones defined in zonedef_table,
 *      and reads each of them from the UniFi chip
 *
 *  Arguments:
 *      card          Pointer to card struct
 *      dump_buf      Buffer into which register values will be dumped
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS                   on success, or:
 *      CSR_RESULT_FAILURE                   SDIO error
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         Parameter error
 *
 *  Notes:
 *      It is assumed that the caller has already stopped the XAPs
 * ---------------------------------------------------------------------------
 */
static CsrResult unifi_coredump_read_zones(card_t *card, coredump_buffer *dump_buf)
{
    CsrResult r = CSR_RESULT_SUCCESS;
    s32 i;

    func_enter();

    /* Walk the table of coredump zone definitions and read them from the chip */
    for (i = 0;
         (i < HIP_CDUMP_NUM_ZONES) && (r == 0);
         i++)
    {
        r = unifi_coredump_read_zone(card, dump_buf->zone[i], &zonedef_table[i]);
    }

    func_exit_r(r);
    return r;
}


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_from_sdio
 *
 *      Capture the status of the UniFi processors, over SDIO
 *
 *  Arguments:
 *      card            Pointer to card struct
 *      reg_buffer      Buffer into which register values will be dumped
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS                   on success, or:
 *      CSR_RESULT_FAILURE                   SDIO error
 *      CSR_WIFI_HIP_RESULT_INVALID_VALUE         Parameter error
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
static CsrResult unifi_coredump_from_sdio(card_t *card, coredump_buffer *dump_buf)
{
    u16 val;
    CsrResult r;
    u32 sdio_addr;

    func_enter();

    if (dump_buf == NULL)
    {
        r = CSR_WIFI_HIP_RESULT_INVALID_VALUE;
        goto done;
    }


    /* Chip and firmware version */
    unifi_trace(card->ospriv, UDBG4, "Get chip version\n");
    sdio_addr = 2 * ChipHelper_GBL_CHIP_VERSION(card->helper);
    if (sdio_addr != 0)
    {
        r = unifi_read_direct16(card, sdio_addr, &val);
        if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
        {
            goto done;
        }
        if (r != CSR_RESULT_SUCCESS)
        {
            unifi_error(card->ospriv, "Can't read GBL_CHIP_VERSION\n");
            goto done;
        }
    }
    dump_buf->chip_ver = val;
    dump_buf->fw_ver = card->build_id;

    unifi_trace(card->ospriv, UDBG4, "chip_ver 0x%04x, fw_ver %u\n",
                dump_buf->chip_ver, dump_buf->fw_ver);

    /* Capture the memory zones required from UniFi */
    r = unifi_coredump_read_zones(card, dump_buf);
    if (r == CSR_WIFI_HIP_RESULT_NO_DEVICE)
    {
        goto done;
    }
    if (r != CSR_RESULT_SUCCESS)
    {
        unifi_error(card->ospriv, "Can't read UniFi memory areas\n");
        goto done;
    }

done:
    func_exit_r(r);
    return r;
} /* unifi_coredump_from_sdio() */


#ifndef UNIFI_DISABLE_COREDUMP
/*
 * ---------------------------------------------------------------------------
 *  new_coredump_node
 *
 *      Allocates a coredump linked-list node, and links it to the previous.
 *
 *  Arguments:
 *      ospriv          OS context
 *      prevnode        Previous node to link into
 *
 *  Returns:
 *      Pointer to valid coredump_buffer on success
 *      NULL on memory allocation failure
 *
 *  Notes:
 *      Allocates "all or nothing"
 * ---------------------------------------------------------------------------
 */
static
coredump_buffer* new_coredump_node(void *ospriv, coredump_buffer *prevnode)
{
    coredump_buffer *newnode = NULL;
    u16 *newzone = NULL;
    s32 i;
    u32 zone_size;

    /* Allocate node header */
    newnode = kzalloc(sizeof(coredump_buffer), GFP_KERNEL);
    if (newnode == NULL)
    {
        return NULL;
    }

    /* Allocate chip memory zone capture buffers */
    for (i = 0; i < HIP_CDUMP_NUM_ZONES; i++)
    {
        zone_size = sizeof(u16) * zonedef_table[i].length;
        newzone = kzalloc(zone_size, GFP_KERNEL);
        newnode->zone[i] = newzone;
        if (newzone == NULL)
        {
            unifi_error(ospriv, "Out of memory on coredump zone %d (%d words)\n",
                        i, zonedef_table[i].length);
            break;
        }
    }

    /* Clean up if any zone alloc failed */
    if (newzone == NULL)
    {
        for (i = 0; newnode->zone[i] != NULL; i++)
        {
            kfree(newnode->zone[i]);
            newnode->zone[i] = NULL;
        }
    }

    /* Link to previous node */
    newnode->prev = prevnode;
    if (prevnode)
    {
        prevnode->next = newnode;
    }
    newnode->next = NULL;

    return newnode;
}


#endif /* UNIFI_DISABLE_COREDUMP */

/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_init
 *
 *      Allocates buffers for the automatic SDIO core dump
 *
 *  Arguments:
 *      card                Pointer to card struct
 *      num_dump_buffers    Number of buffers to reserve for coredumps
 *
 *  Returns:
 *      CSR_RESULT_SUCCESS               on success, or:
 *      CSR_WIFI_HIP_RESULT_NO_MEMORY         memory allocation failed
 *
 *  Notes:
 *      Allocates space in advance, to be used for the last n coredump buffers
 *      the intention being that the size is sufficient for at least one dump,
 *      probably several.
 *      It's probably advisable to have at least 2 coredump buffers to allow
 *      one to be enquired with the unifi_coredump tool, while leaving another
 *      free for capturing.
 * ---------------------------------------------------------------------------
 */
CsrResult unifi_coredump_init(card_t *card, u16 num_dump_buffers)
{
#ifndef UNIFI_DISABLE_COREDUMP
    void *ospriv = card->ospriv;
    coredump_buffer *prev = NULL;
    coredump_buffer *newnode = NULL;
    u32 i = 0;
#endif

    func_enter();

    card->request_coredump_on_reset = 0;
    card->dump_next_write = NULL;
    card->dump_cur_read = NULL;
    card->dump_buf = NULL;

#ifndef UNIFI_DISABLE_COREDUMP
    unifi_trace(ospriv, UDBG1,
                "Allocate buffers for %d core dumps\n", num_dump_buffers);
    if (num_dump_buffers == 0)
    {
        goto done;
    }

    /* Root node */
    card->dump_buf = new_coredump_node(ospriv, NULL);
    if (card->dump_buf == NULL)
    {
        goto fail;
    }
    prev = card->dump_buf;
    newnode = card->dump_buf;

    /* Add each subsequent node at tail */
    for (i = 1; i < num_dump_buffers; i++)
    {
        newnode = new_coredump_node(ospriv, prev);
        if (newnode == NULL)
        {
            goto fail;
        }
        prev = newnode;
    }

    /* Link the first and last nodes to make the list circular */
    card->dump_buf->prev = newnode;
    newnode->next = card->dump_buf;

    /* Set initial r/w access pointers */
    card->dump_next_write = card->dump_buf;
    card->dump_cur_read = NULL;

    unifi_trace(ospriv, UDBG2, "Core dump configured (%d dumps max)\n", i);

done:
#endif
    func_exit();
    return CSR_RESULT_SUCCESS;

#ifndef UNIFI_DISABLE_COREDUMP
fail:
    /* Unwind what we allocated so far */
    unifi_error(ospriv, "Out of memory allocating core dump node %d\n", i);
    unifi_coredump_free(card);
    func_exit();
    return CSR_WIFI_HIP_RESULT_NO_MEMORY;
#endif
} /* unifi_coreump_init() */


/*
 * ---------------------------------------------------------------------------
 *  unifi_coredump_free
 *
 *      Free all memory dynamically allocated for core dump
 *
 *  Arguments:
 *      card            Pointer to card struct
 *
 *  Returns:
 *      None
 *
 *  Notes:
 * ---------------------------------------------------------------------------
 */
void unifi_coredump_free(card_t *card)
{
    void *ospriv = card->ospriv;
    coredump_buffer *node, *del_node;
    s16 i = 0;
    s16 j;

    func_enter();
    unifi_trace(ospriv, UDBG2, "Core dump de-configured\n");

    if (card->dump_buf == NULL)
    {
        return;
    }

    node = card->dump_buf;
    do
    {
        /* Free payload zones */
        for (j = 0; j < HIP_CDUMP_NUM_ZONES; j++)
        {
            kfree(node->zone[j]);
            node->zone[j] = NULL;
        }

        /* Detach */
        del_node = node;
        node = node->next;

        /* Free header */
        kfree(del_node);
        i++;
    } while ((node != NULL) && (node != card->dump_buf));

    unifi_trace(ospriv, UDBG3, "Freed %d coredump buffers\n", i);

    card->dump_buf = NULL;
    card->dump_next_write = NULL;
    card->dump_cur_read = NULL;

    func_exit();
} /* unifi_coredump_free() */


