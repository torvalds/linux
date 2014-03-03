_otg_/* =========================================================================
 * $File: //dwh/usb_iip/dev/software/dwc_common_port_2/dwc_cc.c $
 * $Revision: #4 $
 * $Date: 2010/11/04 $
 * $Change: 1621692 $
 *
 * Synopsys Portability Library Software and documentation
 * (hereinafter, "Software") is an Unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing
 * between Synopsys and you.
 *
 * The Software IS NOT an item of Licensed Software or Licensed Product
 * under any End User Software License Agreement or Agreement for
 * Licensed Product with Synopsys or any supplement thereto. You are
 * permitted to use and redistribute this Software in source and binary
 * forms, with or without modification, provided that redistributions
 * of source code must retain this notice. You may not view, use,
 * disclose, copy or distribute this file or any information contained
 * herein except pursuant to this license grant from Synopsys. If you
 * do not agree with this notice, including the disclaimer below, then
 * you are not authorized to use the Software.
 *
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 * BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL
 * SYNOPSYS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================= */
#ifdef DWC_CCLIB

#include "dwc_cc.h"

typedef struct dwc_cc
{
	uint32_t uid;
	uint8_t chid[16];
	uint8_t cdid[16];
	uint8_t ck[16];
	uint8_t *name;
	uint8_t length;
        DWC_CIRCLEQ_ENTRY(dwc_cc) list_entry;
} dwc_cc_t;

DWC_CIRCLEQ_HEAD(context_list, dwc_cc);

/** The main structure for CC management.  */
struct dwc_cc_if
{
	dwc_mutex_t *mutex;
	char *filename;

	unsigned is_host:1;

	dwc_notifier_t *notifier;

	struct context_list list;
};

#ifdef DEBUG
static inline void dump_bytes(char *name, uint8_t *bytes, int len)
{
	int i;
	DWC_PRINTF("%s: ", name);
	for (i=0; i<len; i++) {
		DWC_PRINTF("%02x ", bytes[i]);
	}
	DWC_PRINTF("\n");
}
#else
#define dump_bytes(x...)
#endif

static dwc_cc_t *alloc_cc(void *mem_ctx, uint8_t *name, uint32_t length)
{
	dwc_cc_t *cc = dwc_alloc(mem_ctx, sizeof(dwc_cc_t));
	if (!cc) {
		return NULL;
	}
	DWC_MEMSET(cc, 0, sizeof(dwc_cc_t));

	if (name) {
		cc->length = length;
		cc->name = dwc_alloc(mem_ctx, length);
		if (!cc->name) {
			dwc_free(mem_ctx, cc);
			return NULL;
		}

		DWC_MEMCPY(cc->name, name, length);
	}

	return cc;
}

static void free_cc(void *mem_ctx, dwc_cc_t *cc)
{
	if (cc->name) {
		dwc_free(mem_ctx, cc->name);
	}
	dwc_free(mem_ctx, cc);
}

static uint32_t next_uid(dwc_cc_if_t *cc_if)
{
	uint32_t uid = 0;
	dwc_cc_t *cc;
	DWC_CIRCLEQ_FOREACH(cc, &cc_if->list, list_entry) {
		if (cc->uid > uid) {
			uid = cc->uid;
		}
	}

	if (uid == 0) {
		uid = 255;
	}

	return uid + 1;
}

static dwc_cc_t *cc_find(dwc_cc_if_t *cc_if, uint32_t uid)
{
	dwc_cc_t *cc;
	DWC_CIRCLEQ_FOREACH(cc, &cc_if->list, list_entry) {
		if (cc->uid == uid) {
			return cc;
		}
	}
	return NULL;
}

static unsigned int cc_data_size(dwc_cc_if_t *cc_if)
{
	unsigned int size = 0;
	dwc_cc_t *cc;
	DWC_CIRCLEQ_FOREACH(cc, &cc_if->list, list_entry) {
		size += (48 + 1);
		if (cc->name) {
			size += cc->length;
		}
	}
	return size;
}

static uint32_t cc_match_chid(dwc_cc_if_t *cc_if, uint8_t *chid)
{
	uint32_t uid = 0;
	dwc_cc_t *cc;

	DWC_CIRCLEQ_FOREACH(cc, &cc_if->list, list_entry) {
		if (DWC_MEMCMP(cc->chid, chid, 16) == 0) {
			uid = cc->uid;
			break;
		}
	}
	return uid;
}
static uint32_t cc_match_cdid(dwc_cc_if_t *cc_if, uint8_t *cdid)
{
	uint32_t uid = 0;
	dwc_cc_t *cc;

	DWC_CIRCLEQ_FOREACH(cc, &cc_if->list, list_entry) {
		if (DWC_MEMCMP(cc->cdid, cdid, 16) == 0) {
			uid = cc->uid;
			break;
		}
	}
	return uid;
}

/* Internal cc_add */
static int32_t cc_add(void *mem_ctx, dwc_cc_if_t *cc_if, uint8_t *chid,
		      uint8_t *cdid, uint8_t *ck, uint8_t *name, uint8_t length)
{
	dwc_cc_t *cc;
	uint32_t uid;

	if (cc_if->is_host) {
		uid = cc_match_cdid(cc_if, cdid);
	}
	else {
		uid = cc_match_chid(cc_if, chid);
	}

	if (uid) {
		DWC_DEBUG("Replacing previous connection context id=%d name=%p name_len=%d", uid, name, length);
		cc = cc_find(cc_if, uid);
	}
	else {
		cc = alloc_cc(mem_ctx, name, length);
		cc->uid = next_uid(cc_if);
		DWC_CIRCLEQ_INSERT_TAIL(&cc_if->list, cc, list_entry);
	}

	DWC_MEMCPY(&(cc->chid[0]), chid, 16);
	DWC_MEMCPY(&(cc->cdid[0]), cdid, 16);
	DWC_MEMCPY(&(cc->ck[0]), ck, 16);

	DWC_DEBUG("Added connection context id=%d name=%p name_len=%d", cc->uid, name, length);
	dump_bytes("CHID", cc->chid, 16);
	dump_bytes("CDID", cc->cdid, 16);
	dump_bytes("CK", cc->ck, 16);
	return cc->uid;
}

/* Internal cc_clear */
static void cc_clear(void *mem_ctx, dwc_cc_if_t *cc_if)
{
	while (!DWC_CIRCLEQ_EMPTY(&cc_if->list)) {
		dwc_cc_t *cc = DWC_CIRCLEQ_FIRST(&cc_if->list);
		DWC_CIRCLEQ_REMOVE_INIT(&cc_if->list, cc, list_entry);
		free_cc(mem_ctx, cc);
	}
}

dwc_cc_if_t *dwc_cc_if_alloc(void *mem_ctx, void *mtx_ctx, 
			     dwc_notifier_t *notifier, unsigned is_host)
{
	dwc_cc_if_t *cc_if = NULL;

	/* Allocate a common_cc_if structure */
	cc_if = dwc_alloc(mem_ctx, sizeof(dwc_cc_if_t));

	if (!cc_if)
		return NULL;

#if (defined(DWC_LINUX) && defined(CONFIG_DEBUG_MUTEXES))
	DWC_MUTEX_ALLOC_LINUX_DEBUG(cc_if->mutex);
#else
	cc_if->mutex = dwc_mutex_alloc(mtx_ctx);
#endif
	if (!cc_if->mutex) {
		dwc_free(mem_ctx, cc_if);
		return NULL;
	}

	DWC_CIRCLEQ_INIT(&cc_if->list);
	cc_if->is_host = is_host;
	cc_if->notifier = notifier;
	return cc_if;
}

void dwc_cc_if_free(void *mem_ctx, void *mtx_ctx, dwc_cc_if_t *cc_if)
{
#if (defined(DWC_LINUX) && defined(CONFIG_DEBUG_MUTEXES))
	DWC_MUTEX_FREE(cc_if->mutex);
#else
	dwc_mutex_free(mtx_ctx, cc_if->mutex);
#endif
	cc_clear(mem_ctx, cc_if);
	dwc_free(mem_ctx, cc_if);
}

static void cc_changed(dwc_cc_if_t *cc_if)
{
	if (cc_if->notifier) {
		dwc_notify(cc_if->notifier, DWC_CC_LIST_CHANGED_NOTIFICATION, cc_if);
	}
}

void dwc_cc_clear(void *mem_ctx, dwc_cc_if_t *cc_if)
{
	DWC_MUTEX_LOCK(cc_if->mutex);
	cc_clear(mem_ctx, cc_if);
	DWC_MUTEX_UNLOCK(cc_if->mutex);
	cc_changed(cc_if);
}

int32_t dwc_cc_add(void *mem_ctx, dwc_cc_if_t *cc_if, uint8_t *chid,
		   uint8_t *cdid, uint8_t *ck, uint8_t *name, uint8_t length)
{
	uint32_t uid;

	DWC_MUTEX_LOCK(cc_if->mutex);
	uid = cc_add(mem_ctx, cc_if, chid, cdid, ck, name, length);
	DWC_MUTEX_UNLOCK(cc_if->mutex);
	cc_changed(cc_if);

	return uid;
}

void dwc_cc_change(void *mem_ctx, dwc_cc_if_t *cc_if, int32_t id, uint8_t *chid,
		   uint8_t *cdid, uint8_t *ck, uint8_t *name, uint8_t length)
{
	dwc_cc_t* cc;

	DWC_DEBUG("Change connection context %d", id);

	DWC_MUTEX_LOCK(cc_if->mutex);
	cc = cc_find(cc_if, id);
	if (!cc) {
		DWC_ERROR("Uid %d not found in cc list\n", id);
		DWC_MUTEX_UNLOCK(cc_if->mutex);
		return;
	}

	if (chid) {
		DWC_MEMCPY(&(cc->chid[0]), chid, 16);
	}
	if (cdid) {
		DWC_MEMCPY(&(cc->cdid[0]), cdid, 16);
	}
	if (ck) {
		DWC_MEMCPY(&(cc->ck[0]), ck, 16);
	}

	if (name) {
		if (cc->name) {
			dwc_free(mem_ctx, cc->name);
		}
		cc->name = dwc_alloc(mem_ctx, length);
		if (!cc->name) {
			DWC_ERROR("Out of memory in dwc_cc_change()\n");
			DWC_MUTEX_UNLOCK(cc_if->mutex);
			return;
		}
		cc->length = length;
		DWC_MEMCPY(cc->name, name, length);
	}

	DWC_MUTEX_UNLOCK(cc_if->mutex);

	cc_changed(cc_if);

	DWC_DEBUG("Changed connection context id=%d\n", id);
	dump_bytes("New CHID", cc->chid, 16);
	dump_bytes("New CDID", cc->cdid, 16);
	dump_bytes("New CK", cc->ck, 16);
}

void dwc_cc_remove(void *mem_ctx, dwc_cc_if_t *cc_if, int32_t id)
{
	dwc_cc_t *cc;

	DWC_DEBUG("Removing connection context %d", id);

	DWC_MUTEX_LOCK(cc_if->mutex);
	cc = cc_find(cc_if, id);
	if (!cc) {
		DWC_ERROR("Uid %d not found in cc list\n", id);
		DWC_MUTEX_UNLOCK(cc_if->mutex);
		return;
	}

	DWC_CIRCLEQ_REMOVE_INIT(&cc_if->list, cc, list_entry);
	DWC_MUTEX_UNLOCK(cc_if->mutex);
	free_cc(mem_ctx, cc);

	cc_changed(cc_if);
}

uint8_t *dwc_cc_data_for_save(void *mem_ctx, dwc_cc_if_t *cc_if, unsigned int *length)
{
	uint8_t *buf, *x;
	uint8_t zero = 0;
	dwc_cc_t *cc;

	DWC_MUTEX_LOCK(cc_if->mutex);
	*length = cc_data_size(cc_if);
	if (!(*length)) {
		DWC_MUTEX_UNLOCK(cc_if->mutex);
		return NULL;
	}

	DWC_DEBUG("Creating data for saving (length=%d)", *length);

	buf = dwc_alloc(mem_ctx, *length);
	if (!buf) {
		*length = 0;
		DWC_MUTEX_UNLOCK(cc_if->mutex);
		return NULL;
	}

	x = buf;
	DWC_CIRCLEQ_FOREACH(cc, &cc_if->list, list_entry) {
		DWC_MEMCPY(x, cc->chid, 16);
		x += 16;
		DWC_MEMCPY(x, cc->cdid, 16);
		x += 16;
		DWC_MEMCPY(x, cc->ck, 16);
		x += 16;
		if (cc->name) {
			DWC_MEMCPY(x, &cc->length, 1);
			x += 1;
			DWC_MEMCPY(x, cc->name, cc->length);
			x += cc->length;
		}
		else {
			DWC_MEMCPY(x, &zero, 1);
			x += 1;
		}
	}
	DWC_MUTEX_UNLOCK(cc_if->mutex);

	return buf;
}

void dwc_cc_restore_from_data(void *mem_ctx, dwc_cc_if_t *cc_if, uint8_t *data, uint32_t length)
{
	uint8_t name_length;
	uint8_t *name;
	uint8_t *chid;
	uint8_t *cdid;
	uint8_t *ck;
	uint32_t i = 0;

	DWC_MUTEX_LOCK(cc_if->mutex);
	cc_clear(mem_ctx, cc_if);

	while (i < length) {
		chid = &data[i];
		i += 16;
		cdid = &data[i];
		i += 16;
		ck = &data[i];
		i += 16;

		name_length = data[i];
		i ++;

		if (name_length) {
			name = &data[i];
			i += name_length;
		}
		else {
			name = NULL;
		}

		/* check to see if we haven't overflown the buffer */
		if (i > length) {
			DWC_ERROR("Data format error while attempting to load CCs "
				  "(nlen=%d, iter=%d, buflen=%d).\n", name_length, i, length);
			break;
		}

		cc_add(mem_ctx, cc_if, chid, cdid, ck, name, name_length);
	}
	DWC_MUTEX_UNLOCK(cc_if->mutex);

	cc_changed(cc_if);
}

uint32_t dwc_cc_match_chid(dwc_cc_if_t *cc_if, uint8_t *chid)
{
	uint32_t uid = 0;

	DWC_MUTEX_LOCK(cc_if->mutex);
	uid = cc_match_chid(cc_if, chid);
	DWC_MUTEX_UNLOCK(cc_if->mutex);
	return uid;
}
uint32_t dwc_cc_match_cdid(dwc_cc_if_t *cc_if, uint8_t *cdid)
{
	uint32_t uid = 0;

	DWC_MUTEX_LOCK(cc_if->mutex);
	uid = cc_match_cdid(cc_if, cdid);
	DWC_MUTEX_UNLOCK(cc_if->mutex);
	return uid;
}

uint8_t *dwc_cc_ck(dwc_cc_if_t *cc_if, int32_t id)
{
	uint8_t *ck = NULL;
	dwc_cc_t *cc;

	DWC_MUTEX_LOCK(cc_if->mutex);
	cc = cc_find(cc_if, id);
	if (cc) {
		ck = cc->ck;
	}
	DWC_MUTEX_UNLOCK(cc_if->mutex);

	return ck;

}

uint8_t *dwc_cc_chid(dwc_cc_if_t *cc_if, int32_t id)
{
	uint8_t *retval = NULL;
	dwc_cc_t *cc;

	DWC_MUTEX_LOCK(cc_if->mutex);
	cc = cc_find(cc_if, id);
	if (cc) {
		retval = cc->chid;
	}
	DWC_MUTEX_UNLOCK(cc_if->mutex);

	return retval;
}

uint8_t *dwc_cc_cdid(dwc_cc_if_t *cc_if, int32_t id)
{
	uint8_t *retval = NULL;
	dwc_cc_t *cc;

	DWC_MUTEX_LOCK(cc_if->mutex);
	cc = cc_find(cc_if, id);
	if (cc) {
		retval = cc->cdid;
	}
	DWC_MUTEX_UNLOCK(cc_if->mutex);

	return retval;
}

uint8_t *dwc_cc_name(dwc_cc_if_t *cc_if, int32_t id, uint8_t *length)
{
	uint8_t *retval = NULL;
	dwc_cc_t *cc;

	DWC_MUTEX_LOCK(cc_if->mutex);
	*length = 0;
	cc = cc_find(cc_if, id);
	if (cc) {
		*length = cc->length;
		retval = cc->name;
	}
	DWC_MUTEX_UNLOCK(cc_if->mutex);

	return retval;
}

#endif	/* DWC_CCLIB */
