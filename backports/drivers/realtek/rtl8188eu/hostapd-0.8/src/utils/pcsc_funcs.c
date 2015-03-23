/*
 * WPA Supplicant / PC/SC smartcard interface for USIM, GSM SIM
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file implements wrapper functions for accessing GSM SIM and 3GPP USIM
 * cards through PC/SC smartcard library. These functions are used to implement
 * authentication routines for EAP-SIM and EAP-AKA.
 */

#include "includes.h"
#include <winscard.h>

#include "common.h"
#include "pcsc_funcs.h"


/* See ETSI GSM 11.11 and ETSI TS 102 221 for details.
 * SIM commands:
 * Command APDU: CLA INS P1 P2 P3 Data
 *   CLA (class of instruction): A0 for GSM, 00 for USIM
 *   INS (instruction)
 *   P1 P2 P3 (parameters, P3 = length of Data)
 * Response APDU: Data SW1 SW2
 *   SW1 SW2 (Status words)
 * Commands (INS P1 P2 P3):
 *   SELECT: A4 00 00 02 <file_id, 2 bytes>
 *   GET RESPONSE: C0 00 00 <len>
 *   RUN GSM ALG: 88 00 00 00 <RAND len = 10>
 *   RUN UMTS ALG: 88 00 81 <len=0x22> data: 0x10 | RAND | 0x10 | AUTN
 *	P1 = ID of alg in card
 *	P2 = ID of secret key
 *   READ BINARY: B0 <offset high> <offset low> <len>
 *   READ RECORD: B2 <record number> <mode> <len>
 *	P2 (mode) = '02' (next record), '03' (previous record),
 *		    '04' (absolute mode)
 *   VERIFY CHV: 20 00 <CHV number> 08
 *   CHANGE CHV: 24 00 <CHV number> 10
 *   DISABLE CHV: 26 00 01 08
 *   ENABLE CHV: 28 00 01 08
 *   UNBLOCK CHV: 2C 00 <00=CHV1, 02=CHV2> 10
 *   SLEEP: FA 00 00 00
 */

/* GSM SIM commands */
#define SIM_CMD_SELECT			0xa0, 0xa4, 0x00, 0x00, 0x02
#define SIM_CMD_RUN_GSM_ALG		0xa0, 0x88, 0x00, 0x00, 0x10
#define SIM_CMD_GET_RESPONSE		0xa0, 0xc0, 0x00, 0x00
#define SIM_CMD_READ_BIN		0xa0, 0xb0, 0x00, 0x00
#define SIM_CMD_READ_RECORD		0xa0, 0xb2, 0x00, 0x00
#define SIM_CMD_VERIFY_CHV1		0xa0, 0x20, 0x00, 0x01, 0x08

/* USIM commands */
#define USIM_CLA			0x00
#define USIM_CMD_RUN_UMTS_ALG		0x00, 0x88, 0x00, 0x81, 0x22
#define USIM_CMD_GET_RESPONSE		0x00, 0xc0, 0x00, 0x00

#define SIM_RECORD_MODE_ABSOLUTE 0x04

#define USIM_FSP_TEMPL_TAG		0x62

#define USIM_TLV_FILE_DESC		0x82
#define USIM_TLV_FILE_ID		0x83
#define USIM_TLV_DF_NAME		0x84
#define USIM_TLV_PROPR_INFO		0xA5
#define USIM_TLV_LIFE_CYCLE_STATUS	0x8A
#define USIM_TLV_FILE_SIZE		0x80
#define USIM_TLV_TOTAL_FILE_SIZE	0x81
#define USIM_TLV_PIN_STATUS_TEMPLATE	0xC6
#define USIM_TLV_SHORT_FILE_ID		0x88

#define USIM_PS_DO_TAG			0x90

#define AKA_RAND_LEN 16
#define AKA_AUTN_LEN 16
#define AKA_AUTS_LEN 14
#define RES_MAX_LEN 16
#define IK_LEN 16
#define CK_LEN 16


typedef enum { SCARD_GSM_SIM, SCARD_USIM } sim_types;

struct scard_data {
	SCARDCONTEXT ctx;
	SCARDHANDLE card;
	DWORD protocol;
	sim_types sim_type;
	int pin1_required;
};

#ifdef __MINGW32_VERSION
/* MinGW does not yet support WinScard, so load the needed functions
 * dynamically from winscard.dll for now. */

static HINSTANCE dll = NULL; /* winscard.dll */

static const SCARD_IO_REQUEST *dll_g_rgSCardT0Pci, *dll_g_rgSCardT1Pci;
#undef SCARD_PCI_T0
#define SCARD_PCI_T0 (dll_g_rgSCardT0Pci)
#undef SCARD_PCI_T1
#define SCARD_PCI_T1 (dll_g_rgSCardT1Pci)


static WINSCARDAPI LONG WINAPI
(*dll_SCardEstablishContext)(IN DWORD dwScope,
			     IN LPCVOID pvReserved1,
			     IN LPCVOID pvReserved2,
			     OUT LPSCARDCONTEXT phContext);
#define SCardEstablishContext dll_SCardEstablishContext

static long (*dll_SCardReleaseContext)(long hContext);
#define SCardReleaseContext dll_SCardReleaseContext

static WINSCARDAPI LONG WINAPI
(*dll_SCardListReadersA)(IN SCARDCONTEXT hContext,
			 IN LPCSTR mszGroups,
			 OUT LPSTR mszReaders,
			 IN OUT LPDWORD pcchReaders);
#undef SCardListReaders
#define SCardListReaders dll_SCardListReadersA

static WINSCARDAPI LONG WINAPI
(*dll_SCardConnectA)(IN SCARDCONTEXT hContext,
		     IN LPCSTR szReader,
		     IN DWORD dwShareMode,
		     IN DWORD dwPreferredProtocols,
		     OUT LPSCARDHANDLE phCard,
		     OUT LPDWORD pdwActiveProtocol);
#undef SCardConnect
#define SCardConnect dll_SCardConnectA

static WINSCARDAPI LONG WINAPI
(*dll_SCardDisconnect)(IN SCARDHANDLE hCard,
		       IN DWORD dwDisposition);
#define SCardDisconnect dll_SCardDisconnect

static WINSCARDAPI LONG WINAPI
(*dll_SCardTransmit)(IN SCARDHANDLE hCard,
		     IN LPCSCARD_IO_REQUEST pioSendPci,
		     IN LPCBYTE pbSendBuffer,
		     IN DWORD cbSendLength,
		     IN OUT LPSCARD_IO_REQUEST pioRecvPci,
		     OUT LPBYTE pbRecvBuffer,
		     IN OUT LPDWORD pcbRecvLength);
#define SCardTransmit dll_SCardTransmit

static WINSCARDAPI LONG WINAPI
(*dll_SCardBeginTransaction)(IN SCARDHANDLE hCard);
#define SCardBeginTransaction dll_SCardBeginTransaction

static WINSCARDAPI LONG WINAPI
(*dll_SCardEndTransaction)(IN SCARDHANDLE hCard, IN DWORD dwDisposition);
#define SCardEndTransaction dll_SCardEndTransaction


static int mingw_load_symbols(void)
{
	char *sym;

	if (dll)
		return 0;

	dll = LoadLibrary("winscard");
	if (dll == NULL) {
		wpa_printf(MSG_DEBUG, "WinSCard: Could not load winscard.dll "
			   "library");
		return -1;
	}

#define LOADSYM(s) \
	sym = #s; \
	dll_ ## s = (void *) GetProcAddress(dll, sym); \
	if (dll_ ## s == NULL) \
		goto fail;

	LOADSYM(SCardEstablishContext);
	LOADSYM(SCardReleaseContext);
	LOADSYM(SCardListReadersA);
	LOADSYM(SCardConnectA);
	LOADSYM(SCardDisconnect);
	LOADSYM(SCardTransmit);
	LOADSYM(SCardBeginTransaction);
	LOADSYM(SCardEndTransaction);
	LOADSYM(g_rgSCardT0Pci);
	LOADSYM(g_rgSCardT1Pci);

#undef LOADSYM

	return 0;

fail:
	wpa_printf(MSG_DEBUG, "WinSCard: Could not get address for %s from "
		   "winscard.dll", sym);
	FreeLibrary(dll);
	dll = NULL;
	return -1;
}


static void mingw_unload_symbols(void)
{
	if (dll == NULL)
		return;

	FreeLibrary(dll);
	dll = NULL;
}

#else /* __MINGW32_VERSION */

#define mingw_load_symbols() 0
#define mingw_unload_symbols() do { } while (0)

#endif /* __MINGW32_VERSION */


static int _scard_select_file(struct scard_data *scard, unsigned short file_id,
			      unsigned char *buf, size_t *buf_len,
			      sim_types sim_type, unsigned char *aid,
			      size_t aidlen);
static int scard_select_file(struct scard_data *scard, unsigned short file_id,
			     unsigned char *buf, size_t *buf_len);
static int scard_verify_pin(struct scard_data *scard, const char *pin);
static int scard_get_record_len(struct scard_data *scard,
				unsigned char recnum, unsigned char mode);
static int scard_read_record(struct scard_data *scard,
			     unsigned char *data, size_t len,
			     unsigned char recnum, unsigned char mode);


static int scard_parse_fsp_templ(unsigned char *buf, size_t buf_len,
				 int *ps_do, int *file_len)
{
		unsigned char *pos, *end;

		if (ps_do)
			*ps_do = -1;
		if (file_len)
			*file_len = -1;

		pos = buf;
		end = pos + buf_len;
		if (*pos != USIM_FSP_TEMPL_TAG) {
			wpa_printf(MSG_DEBUG, "SCARD: file header did not "
				   "start with FSP template tag");
			return -1;
		}
		pos++;
		if (pos >= end)
			return -1;
		if ((pos + pos[0]) < end)
			end = pos + 1 + pos[0];
		pos++;
		wpa_hexdump(MSG_DEBUG, "SCARD: file header FSP template",
			    pos, end - pos);

		while (pos + 1 < end) {
			wpa_printf(MSG_MSGDUMP, "SCARD: file header TLV "
				   "0x%02x len=%d", pos[0], pos[1]);
			if (pos + 2 + pos[1] > end)
				break;

			if (pos[0] == USIM_TLV_FILE_SIZE &&
			    (pos[1] == 1 || pos[1] == 2) && file_len) {
				if (pos[1] == 1)
					*file_len = (int) pos[2];
				else
					*file_len = ((int) pos[2] << 8) |
						(int) pos[3];
				wpa_printf(MSG_DEBUG, "SCARD: file_size=%d",
					   *file_len);
			}

			if (pos[0] == USIM_TLV_PIN_STATUS_TEMPLATE &&
			    pos[1] >= 2 && pos[2] == USIM_PS_DO_TAG &&
			    pos[3] >= 1 && ps_do) {
				wpa_printf(MSG_DEBUG, "SCARD: PS_DO=0x%02x",
					   pos[4]);
				*ps_do = (int) pos[4];
			}

			pos += 2 + pos[1];

			if (pos == end)
				return 0;
		}
		return -1;
}


static int scard_pin_needed(struct scard_data *scard,
			    unsigned char *hdr, size_t hlen)
{
	if (scard->sim_type == SCARD_GSM_SIM) {
		if (hlen > SCARD_CHV1_OFFSET &&
		    !(hdr[SCARD_CHV1_OFFSET] & SCARD_CHV1_FLAG))
			return 1;
		return 0;
	}

	if (scard->sim_type == SCARD_USIM) {
		int ps_do;
		if (scard_parse_fsp_templ(hdr, hlen, &ps_do, NULL))
			return -1;
		/* TODO: there could be more than one PS_DO entry because of
		 * multiple PINs in key reference.. */
		if (ps_do > 0 && (ps_do & 0x80))
			return 1;
		return 0;
	}

	return -1;
}


static int scard_get_aid(struct scard_data *scard, unsigned char *aid,
			 size_t maxlen)
{
	int rlen, rec;
	struct efdir {
		unsigned char appl_template_tag; /* 0x61 */
		unsigned char appl_template_len;
		unsigned char appl_id_tag; /* 0x4f */
		unsigned char aid_len;
		unsigned char rid[5];
		unsigned char appl_code[2]; /* 0x1002 for 3G USIM */
	} *efdir;
	unsigned char buf[100];
	size_t blen;

	efdir = (struct efdir *) buf;
	blen = sizeof(buf);
	if (scard_select_file(scard, SCARD_FILE_EF_DIR, buf, &blen)) {
		wpa_printf(MSG_DEBUG, "SCARD: Failed to read EF_DIR");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "SCARD: EF_DIR select", buf, blen);

	for (rec = 1; rec < 10; rec++) {
		rlen = scard_get_record_len(scard, rec,
					    SIM_RECORD_MODE_ABSOLUTE);
		if (rlen < 0) {
			wpa_printf(MSG_DEBUG, "SCARD: Failed to get EF_DIR "
				   "record length");
			return -1;
		}
		blen = sizeof(buf);
		if (rlen > (int) blen) {
			wpa_printf(MSG_DEBUG, "SCARD: Too long EF_DIR record");
			return -1;
		}
		if (scard_read_record(scard, buf, rlen, rec,
				      SIM_RECORD_MODE_ABSOLUTE) < 0) {
			wpa_printf(MSG_DEBUG, "SCARD: Failed to read "
				   "EF_DIR record %d", rec);
			return -1;
		}
		wpa_hexdump(MSG_DEBUG, "SCARD: EF_DIR record", buf, rlen);

		if (efdir->appl_template_tag != 0x61) {
			wpa_printf(MSG_DEBUG, "SCARD: Unexpected application "
				   "template tag 0x%x",
				   efdir->appl_template_tag);
			continue;
		}

		if (efdir->appl_template_len > rlen - 2) {
			wpa_printf(MSG_DEBUG, "SCARD: Too long application "
				   "template (len=%d rlen=%d)",
				   efdir->appl_template_len, rlen);
			continue;
		}

		if (efdir->appl_id_tag != 0x4f) {
			wpa_printf(MSG_DEBUG, "SCARD: Unexpected application "
				   "identifier tag 0x%x", efdir->appl_id_tag);
			continue;
		}

		if (efdir->aid_len < 1 || efdir->aid_len > 16) {
			wpa_printf(MSG_DEBUG, "SCARD: Invalid AID length %d",
				   efdir->aid_len);
			continue;
		}

		wpa_hexdump(MSG_DEBUG, "SCARD: AID from EF_DIR record",
			    efdir->rid, efdir->aid_len);

		if (efdir->appl_code[0] == 0x10 &&
		    efdir->appl_code[1] == 0x02) {
			wpa_printf(MSG_DEBUG, "SCARD: 3G USIM app found from "
				   "EF_DIR record %d", rec);
			break;
		}
	}

	if (rec >= 10) {
		wpa_printf(MSG_DEBUG, "SCARD: 3G USIM app not found "
			   "from EF_DIR records");
		return -1;
	}

	if (efdir->aid_len > maxlen) {
		wpa_printf(MSG_DEBUG, "SCARD: Too long AID");
		return -1;
	}

	os_memcpy(aid, efdir->rid, efdir->aid_len);

	return efdir->aid_len;
}


/**
 * scard_init - Initialize SIM/USIM connection using PC/SC
 * @sim_type: Allowed SIM types (SIM, USIM, or both)
 * Returns: Pointer to private data structure, or %NULL on failure
 *
 * This function is used to initialize SIM/USIM connection. PC/SC is used to
 * open connection to the SIM/USIM card and the card is verified to support the
 * selected sim_type. In addition, local flag is set if a PIN is needed to
 * access some of the card functions. Once the connection is not needed
 * anymore, scard_deinit() can be used to close it.
 */
struct scard_data * scard_init(scard_sim_type sim_type)
{
	long ret;
	unsigned long len;
	struct scard_data *scard;
#ifdef CONFIG_NATIVE_WINDOWS
	TCHAR *readers = NULL;
#else /* CONFIG_NATIVE_WINDOWS */
	char *readers = NULL;
#endif /* CONFIG_NATIVE_WINDOWS */
	unsigned char buf[100];
	size_t blen;
	int transaction = 0;
	int pin_needed;

	wpa_printf(MSG_DEBUG, "SCARD: initializing smart card interface");
	if (mingw_load_symbols())
		return NULL;
	scard = os_zalloc(sizeof(*scard));
	if (scard == NULL)
		return NULL;

	ret = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL,
				    &scard->ctx);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_DEBUG, "SCARD: Could not establish smart card "
			   "context (err=%ld)", ret);
		goto failed;
	}

	ret = SCardListReaders(scard->ctx, NULL, NULL, &len);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_DEBUG, "SCARD: SCardListReaders failed "
			   "(err=%ld)", ret);
		goto failed;
	}

#ifdef UNICODE
	len *= 2;
#endif /* UNICODE */
	readers = os_malloc(len);
	if (readers == NULL) {
		wpa_printf(MSG_INFO, "SCARD: malloc failed\n");
		goto failed;
	}

	ret = SCardListReaders(scard->ctx, NULL, readers, &len);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_DEBUG, "SCARD: SCardListReaders failed(2) "
			   "(err=%ld)", ret);
		goto failed;
	}
	if (len < 3) {
		wpa_printf(MSG_WARNING, "SCARD: No smart card readers "
			   "available.");
		goto failed;
	}
	/* readers is a list of available reader. Last entry is terminated with
	 * double NUL.
	 * TODO: add support for selecting the reader; now just use the first
	 * one.. */
#ifdef UNICODE
	wpa_printf(MSG_DEBUG, "SCARD: Selected reader='%S'", readers);
#else /* UNICODE */
	wpa_printf(MSG_DEBUG, "SCARD: Selected reader='%s'", readers);
#endif /* UNICODE */

	ret = SCardConnect(scard->ctx, readers, SCARD_SHARE_SHARED,
			   SCARD_PROTOCOL_T0, &scard->card, &scard->protocol);
	if (ret != SCARD_S_SUCCESS) {
		if (ret == (long) SCARD_E_NO_SMARTCARD)
			wpa_printf(MSG_INFO, "No smart card inserted.");
		else
			wpa_printf(MSG_WARNING, "SCardConnect err=%lx", ret);
		goto failed;
	}

	os_free(readers);
	readers = NULL;

	wpa_printf(MSG_DEBUG, "SCARD: card=0x%x active_protocol=%lu (%s)",
		   (unsigned int) scard->card, scard->protocol,
		   scard->protocol == SCARD_PROTOCOL_T0 ? "T0" : "T1");

	ret = SCardBeginTransaction(scard->card);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_DEBUG, "SCARD: Could not begin transaction: "
			   "0x%x", (unsigned int) ret);
		goto failed;
	}
	transaction = 1;

	blen = sizeof(buf);

	scard->sim_type = SCARD_GSM_SIM;
	if (sim_type == SCARD_USIM_ONLY || sim_type == SCARD_TRY_BOTH) {
		wpa_printf(MSG_DEBUG, "SCARD: verifying USIM support");
		if (_scard_select_file(scard, SCARD_FILE_MF, buf, &blen,
				       SCARD_USIM, NULL, 0)) {
			wpa_printf(MSG_DEBUG, "SCARD: USIM is not supported");
			if (sim_type == SCARD_USIM_ONLY)
				goto failed;
			wpa_printf(MSG_DEBUG, "SCARD: Trying to use GSM SIM");
			scard->sim_type = SCARD_GSM_SIM;
		} else {
			wpa_printf(MSG_DEBUG, "SCARD: USIM is supported");
			scard->sim_type = SCARD_USIM;
		}
	}

	if (scard->sim_type == SCARD_GSM_SIM) {
		blen = sizeof(buf);
		if (scard_select_file(scard, SCARD_FILE_MF, buf, &blen)) {
			wpa_printf(MSG_DEBUG, "SCARD: Failed to read MF");
			goto failed;
		}

		blen = sizeof(buf);
		if (scard_select_file(scard, SCARD_FILE_GSM_DF, buf, &blen)) {
			wpa_printf(MSG_DEBUG, "SCARD: Failed to read GSM DF");
			goto failed;
		}
	} else {
		unsigned char aid[32];
		int aid_len;

		aid_len = scard_get_aid(scard, aid, sizeof(aid));
		if (aid_len < 0) {
			wpa_printf(MSG_DEBUG, "SCARD: Failed to find AID for "
				   "3G USIM app - try to use standard 3G RID");
			os_memcpy(aid, "\xa0\x00\x00\x00\x87", 5);
			aid_len = 5;
		}
		wpa_hexdump(MSG_DEBUG, "SCARD: 3G USIM AID", aid, aid_len);

		/* Select based on AID = 3G RID from EF_DIR. This is usually
		 * starting with A0 00 00 00 87. */
		blen = sizeof(buf);
		if (_scard_select_file(scard, 0, buf, &blen, scard->sim_type,
				       aid, aid_len)) {
			wpa_printf(MSG_INFO, "SCARD: Failed to read 3G USIM "
				   "app");
			wpa_hexdump(MSG_INFO, "SCARD: 3G USIM AID",
				    aid, aid_len);
			goto failed;
		}
	}

	/* Verify whether CHV1 (PIN1) is needed to access the card. */
	pin_needed = scard_pin_needed(scard, buf, blen);
	if (pin_needed < 0) {
		wpa_printf(MSG_DEBUG, "SCARD: Failed to determine whether PIN "
			   "is needed");
		goto failed;
	}
	if (pin_needed) {
		scard->pin1_required = 1;
		wpa_printf(MSG_DEBUG, "PIN1 needed for SIM access");
	}

	ret = SCardEndTransaction(scard->card, SCARD_LEAVE_CARD);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_DEBUG, "SCARD: Could not end transaction: "
			   "0x%x", (unsigned int) ret);
	}

	return scard;

failed:
	if (transaction)
		SCardEndTransaction(scard->card, SCARD_LEAVE_CARD);
	os_free(readers);
	scard_deinit(scard);
	return NULL;
}


/**
 * scard_set_pin - Set PIN (CHV1/PIN1) code for accessing SIM/USIM commands
 * @scard: Pointer to private data from scard_init()
 * @pin: PIN code as an ASCII string (e.g., "1234")
 * Returns: 0 on success, -1 on failure
 */
int scard_set_pin(struct scard_data *scard, const char *pin)
{
	if (scard == NULL)
		return -1;

	/* Verify whether CHV1 (PIN1) is needed to access the card. */
	if (scard->pin1_required) {
		if (pin == NULL) {
			wpa_printf(MSG_DEBUG, "No PIN configured for SIM "
				   "access");
			return -1;
		}
		if (scard_verify_pin(scard, pin)) {
			wpa_printf(MSG_INFO, "PIN verification failed for "
				"SIM access");
			return -1;
		}
	}

	return 0;
}


/**
 * scard_deinit - Deinitialize SIM/USIM connection
 * @scard: Pointer to private data from scard_init()
 *
 * This function closes the SIM/USIM connect opened with scard_init().
 */
void scard_deinit(struct scard_data *scard)
{
	long ret;

	if (scard == NULL)
		return;

	wpa_printf(MSG_DEBUG, "SCARD: deinitializing smart card interface");
	if (scard->card) {
		ret = SCardDisconnect(scard->card, SCARD_UNPOWER_CARD);
		if (ret != SCARD_S_SUCCESS) {
			wpa_printf(MSG_DEBUG, "SCARD: Failed to disconnect "
				   "smart card (err=%ld)", ret);
		}
	}

	if (scard->ctx) {
		ret = SCardReleaseContext(scard->ctx);
		if (ret != SCARD_S_SUCCESS) {
			wpa_printf(MSG_DEBUG, "Failed to release smart card "
				   "context (err=%ld)", ret);
		}
	}
	os_free(scard);
	mingw_unload_symbols();
}


static long scard_transmit(struct scard_data *scard,
			   unsigned char *_send, size_t send_len,
			   unsigned char *_recv, size_t *recv_len)
{
	long ret;
	unsigned long rlen;

	wpa_hexdump_key(MSG_DEBUG, "SCARD: scard_transmit: send",
			_send, send_len);
	rlen = *recv_len;
	ret = SCardTransmit(scard->card,
			    scard->protocol == SCARD_PROTOCOL_T1 ?
			    SCARD_PCI_T1 : SCARD_PCI_T0,
			    _send, (unsigned long) send_len,
			    NULL, _recv, &rlen);
	*recv_len = rlen;
	if (ret == SCARD_S_SUCCESS) {
		wpa_hexdump(MSG_DEBUG, "SCARD: scard_transmit: recv",
			    _recv, rlen);
	} else {
		wpa_printf(MSG_WARNING, "SCARD: SCardTransmit failed "
			   "(err=0x%lx)", ret);
	}
	return ret;
}


static int _scard_select_file(struct scard_data *scard, unsigned short file_id,
			      unsigned char *buf, size_t *buf_len,
			      sim_types sim_type, unsigned char *aid,
			      size_t aidlen)
{
	long ret;
	unsigned char resp[3];
	unsigned char cmd[50] = { SIM_CMD_SELECT };
	int cmdlen;
	unsigned char get_resp[5] = { SIM_CMD_GET_RESPONSE };
	size_t len, rlen;

	if (sim_type == SCARD_USIM) {
		cmd[0] = USIM_CLA;
		cmd[3] = 0x04;
		get_resp[0] = USIM_CLA;
	}

	wpa_printf(MSG_DEBUG, "SCARD: select file %04x", file_id);
	if (aid) {
		wpa_hexdump(MSG_DEBUG, "SCARD: select file by AID",
			    aid, aidlen);
		if (5 + aidlen > sizeof(cmd))
			return -1;
		cmd[2] = 0x04; /* Select by AID */
		cmd[4] = aidlen; /* len */
		os_memcpy(cmd + 5, aid, aidlen);
		cmdlen = 5 + aidlen;
	} else {
		cmd[5] = file_id >> 8;
		cmd[6] = file_id & 0xff;
		cmdlen = 7;
	}
	len = sizeof(resp);
	ret = scard_transmit(scard, cmd, cmdlen, resp, &len);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_WARNING, "SCARD: SCardTransmit failed "
			   "(err=0x%lx)", ret);
		return -1;
	}

	if (len != 2) {
		wpa_printf(MSG_WARNING, "SCARD: unexpected resp len "
			   "%d (expected 2)", (int) len);
		return -1;
	}

	if (resp[0] == 0x98 && resp[1] == 0x04) {
		/* Security status not satisfied (PIN_WLAN) */
		wpa_printf(MSG_WARNING, "SCARD: Security status not satisfied "
			   "(PIN_WLAN)");
		return -1;
	}

	if (resp[0] == 0x6e) {
		wpa_printf(MSG_DEBUG, "SCARD: used CLA not supported");
		return -1;
	}

	if (resp[0] != 0x6c && resp[0] != 0x9f && resp[0] != 0x61) {
		wpa_printf(MSG_WARNING, "SCARD: unexpected response 0x%02x "
			   "(expected 0x61, 0x6c, or 0x9f)", resp[0]);
		return -1;
	}
	/* Normal ending of command; resp[1] bytes available */
	get_resp[4] = resp[1];
	wpa_printf(MSG_DEBUG, "SCARD: trying to get response (%d bytes)",
		   resp[1]);

	rlen = *buf_len;
	ret = scard_transmit(scard, get_resp, sizeof(get_resp), buf, &rlen);
	if (ret == SCARD_S_SUCCESS) {
		*buf_len = resp[1] < rlen ? resp[1] : rlen;
		return 0;
	}

	wpa_printf(MSG_WARNING, "SCARD: SCardTransmit err=0x%lx\n", ret);
	return -1;
}


static int scard_select_file(struct scard_data *scard, unsigned short file_id,
			     unsigned char *buf, size_t *buf_len)
{
	return _scard_select_file(scard, file_id, buf, buf_len,
				  scard->sim_type, NULL, 0);
}


static int scard_get_record_len(struct scard_data *scard, unsigned char recnum,
				unsigned char mode)
{
	unsigned char buf[255];
	unsigned char cmd[5] = { SIM_CMD_READ_RECORD /* , len */ };
	size_t blen;
	long ret;

	if (scard->sim_type == SCARD_USIM)
		cmd[0] = USIM_CLA;
	cmd[2] = recnum;
	cmd[3] = mode;
	cmd[4] = sizeof(buf);

	blen = sizeof(buf);
	ret = scard_transmit(scard, cmd, sizeof(cmd), buf, &blen);
	if (ret != SCARD_S_SUCCESS) {
		wpa_printf(MSG_DEBUG, "SCARD: failed to determine file "
			   "length for record %d", recnum);
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "SCARD: file length determination response",
		    buf, blen);

	if (blen < 2 || buf[0] != 0x6c) {
		wpa_printf(MSG_DEBUG, "SCARD: unexpected response to file "
			   "length determination");
		return -1;
	}

	return buf[1];
}


static int scard_read_record(struct scard_data *scard,
			     unsigned char *data, size_t len,
			     unsigned char recnum, unsigned char mode)
{
	unsigned char cmd[5] = { SIM_CMD_READ_RECORD /* , len */ };
	size_t blen = len + 3;
	unsigned char *buf;
	long ret;

	if (scard->sim_type == SCARD_USIM)
		cmd[0] = USIM_CLA;
	cmd[2] = recnum;
	cmd[3] = mode;
	cmd[4] = len;

	buf = os_malloc(blen);
	if (buf == NULL)
		return -1;

	ret = scard_transmit(scard, cmd, sizeof(cmd), buf, &blen);
	if (ret != SCARD_S_SUCCESS) {
		os_free(buf);
		return -2;
	}
	if (blen != len + 2) {
		wpa_printf(MSG_DEBUG, "SCARD: record read returned unexpected "
			   "length %ld (expected %ld)",
			   (long) blen, (long) len + 2);
		os_free(buf);
		return -3;
	}

	if (buf[len] != 0x90 || buf[len + 1] != 0x00) {
		wpa_printf(MSG_DEBUG, "SCARD: record read returned unexpected "
			   "status %02x %02x (expected 90 00)",
			   buf[len], buf[len + 1]);
		os_free(buf);
		return -4;
	}

	os_memcpy(data, buf, len);
	os_free(buf);

	return 0;
}


static int scard_read_file(struct scard_data *scard,
			   unsigned char *data, size_t len)
{
	unsigned char cmd[5] = { SIM_CMD_READ_BIN /* , len */ };
	size_t blen = len + 3;
	unsigned char *buf;
	long ret;

	cmd[4] = len;

	buf = os_malloc(blen);
	if (buf == NULL)
		return -1;

	if (scard->sim_type == SCARD_USIM)
		cmd[0] = USIM_CLA;
	ret = scard_transmit(scard, cmd, sizeof(cmd), buf, &blen);
	if (ret != SCARD_S_SUCCESS) {
		os_free(buf);
		return -2;
	}
	if (blen != len + 2) {
		wpa_printf(MSG_DEBUG, "SCARD: file read returned unexpected "
			   "length %ld (expected %ld)",
			   (long) blen, (long) len + 2);
		os_free(buf);
		return -3;
	}

	if (buf[len] != 0x90 || buf[len + 1] != 0x00) {
		wpa_printf(MSG_DEBUG, "SCARD: file read returned unexpected "
			   "status %02x %02x (expected 90 00)",
			   buf[len], buf[len + 1]);
		os_free(buf);
		return -4;
	}

	os_memcpy(data, buf, len);
	os_free(buf);

	return 0;
}


static int scard_verify_pin(struct scard_data *scard, const char *pin)
{
	long ret;
	unsigned char resp[3];
	unsigned char cmd[5 + 8] = { SIM_CMD_VERIFY_CHV1 };
	size_t len;

	wpa_printf(MSG_DEBUG, "SCARD: verifying PIN");

	if (pin == NULL || os_strlen(pin) > 8)
		return -1;

	if (scard->sim_type == SCARD_USIM)
		cmd[0] = USIM_CLA;
	os_memcpy(cmd + 5, pin, os_strlen(pin));
	os_memset(cmd + 5 + os_strlen(pin), 0xff, 8 - os_strlen(pin));

	len = sizeof(resp);
	ret = scard_transmit(scard, cmd, sizeof(cmd), resp, &len);
	if (ret != SCARD_S_SUCCESS)
		return -2;

	if (len != 2 || resp[0] != 0x90 || resp[1] != 0x00) {
		wpa_printf(MSG_WARNING, "SCARD: PIN verification failed");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "SCARD: PIN verified successfully");
	return 0;
}


/**
 * scard_get_imsi - Read IMSI from SIM/USIM card
 * @scard: Pointer to private data from scard_init()
 * @imsi: Buffer for IMSI
 * @len: Length of imsi buffer; set to IMSI length on success
 * Returns: 0 on success, -1 if IMSI file cannot be selected, -2 if IMSI file
 * selection returns invalid result code, -3 if parsing FSP template file fails
 * (USIM only), -4 if IMSI does not fit in the provided imsi buffer (len is set
 * to needed length), -5 if reading IMSI file fails.
 *
 * This function can be used to read IMSI from the SIM/USIM card. If the IMSI
 * file is PIN protected, scard_set_pin() must have been used to set the
 * correct PIN code before calling scard_get_imsi().
 */
int scard_get_imsi(struct scard_data *scard, char *imsi, size_t *len)
{
	unsigned char buf[100];
	size_t blen, imsilen, i;
	char *pos;

	wpa_printf(MSG_DEBUG, "SCARD: reading IMSI from (GSM) EF-IMSI");
	blen = sizeof(buf);
	if (scard_select_file(scard, SCARD_FILE_GSM_EF_IMSI, buf, &blen))
		return -1;
	if (blen < 4) {
		wpa_printf(MSG_WARNING, "SCARD: too short (GSM) EF-IMSI "
			   "header (len=%ld)", (long) blen);
		return -2;
	}

	if (scard->sim_type == SCARD_GSM_SIM) {
		blen = (buf[2] << 8) | buf[3];
	} else {
		int file_size;
		if (scard_parse_fsp_templ(buf, blen, NULL, &file_size))
			return -3;
		blen = file_size;
	}
	if (blen < 2 || blen > sizeof(buf)) {
		wpa_printf(MSG_DEBUG, "SCARD: invalid IMSI file length=%ld",
			   (long) blen);
		return -3;
	}

	imsilen = (blen - 2) * 2 + 1;
	wpa_printf(MSG_DEBUG, "SCARD: IMSI file length=%ld imsilen=%ld",
		   (long) blen, (long) imsilen);
	if (blen < 2 || imsilen > *len) {
		*len = imsilen;
		return -4;
	}

	if (scard_read_file(scard, buf, blen))
		return -5;

	pos = imsi;
	*pos++ = '0' + (buf[1] >> 4 & 0x0f);
	for (i = 2; i < blen; i++) {
		unsigned char digit;

		digit = buf[i] & 0x0f;
		if (digit < 10)
			*pos++ = '0' + digit;
		else
			imsilen--;

		digit = buf[i] >> 4 & 0x0f;
		if (digit < 10)
			*pos++ = '0' + digit;
		else
			imsilen--;
	}
	*len = imsilen;

	return 0;
}


/**
 * scard_gsm_auth - Run GSM authentication command on SIM card
 * @scard: Pointer to private data from scard_init()
 * @_rand: 16-byte RAND value from HLR/AuC
 * @sres: 4-byte buffer for SRES
 * @kc: 8-byte buffer for Kc
 * Returns: 0 on success, -1 if SIM/USIM connection has not been initialized,
 * -2 if authentication command execution fails, -3 if unknown response code
 * for authentication command is received, -4 if reading of response fails,
 * -5 if if response data is of unexpected length
 *
 * This function performs GSM authentication using SIM/USIM card and the
 * provided RAND value from HLR/AuC. If authentication command can be completed
 * successfully, SRES and Kc values will be written into sres and kc buffers.
 */
int scard_gsm_auth(struct scard_data *scard, const unsigned char *_rand,
		   unsigned char *sres, unsigned char *kc)
{
	unsigned char cmd[5 + 1 + 16] = { SIM_CMD_RUN_GSM_ALG };
	int cmdlen;
	unsigned char get_resp[5] = { SIM_CMD_GET_RESPONSE };
	unsigned char resp[3], buf[12 + 3 + 2];
	size_t len;
	long ret;

	if (scard == NULL)
		return -1;

	wpa_hexdump(MSG_DEBUG, "SCARD: GSM auth - RAND", _rand, 16);
	if (scard->sim_type == SCARD_GSM_SIM) {
		cmdlen = 5 + 16;
		os_memcpy(cmd + 5, _rand, 16);
	} else {
		cmdlen = 5 + 1 + 16;
		cmd[0] = USIM_CLA;
		cmd[3] = 0x80;
		cmd[4] = 17;
		cmd[5] = 16;
		os_memcpy(cmd + 6, _rand, 16);
	}
	len = sizeof(resp);
	ret = scard_transmit(scard, cmd, cmdlen, resp, &len);
	if (ret != SCARD_S_SUCCESS)
		return -2;

	if ((scard->sim_type == SCARD_GSM_SIM &&
	     (len != 2 || resp[0] != 0x9f || resp[1] != 0x0c)) ||
	    (scard->sim_type == SCARD_USIM &&
	     (len != 2 || resp[0] != 0x61 || resp[1] != 0x0e))) {
		wpa_printf(MSG_WARNING, "SCARD: unexpected response for GSM "
			   "auth request (len=%ld resp=%02x %02x)",
			   (long) len, resp[0], resp[1]);
		return -3;
	}
	get_resp[4] = resp[1];

	len = sizeof(buf);
	ret = scard_transmit(scard, get_resp, sizeof(get_resp), buf, &len);
	if (ret != SCARD_S_SUCCESS)
		return -4;

	if (scard->sim_type == SCARD_GSM_SIM) {
		if (len != 4 + 8 + 2) {
			wpa_printf(MSG_WARNING, "SCARD: unexpected data "
				   "length for GSM auth (len=%ld, expected 14)",
				   (long) len);
			return -5;
		}
		os_memcpy(sres, buf, 4);
		os_memcpy(kc, buf + 4, 8);
	} else {
		if (len != 1 + 4 + 1 + 8 + 2) {
			wpa_printf(MSG_WARNING, "SCARD: unexpected data "
				   "length for USIM auth (len=%ld, "
				   "expected 16)", (long) len);
			return -5;
		}
		if (buf[0] != 4 || buf[5] != 8) {
			wpa_printf(MSG_WARNING, "SCARD: unexpected SREC/Kc "
				   "length (%d %d, expected 4 8)",
				   buf[0], buf[5]);
		}
		os_memcpy(sres, buf + 1, 4);
		os_memcpy(kc, buf + 6, 8);
	}

	wpa_hexdump(MSG_DEBUG, "SCARD: GSM auth - SRES", sres, 4);
	wpa_hexdump(MSG_DEBUG, "SCARD: GSM auth - Kc", kc, 8);

	return 0;
}


/**
 * scard_umts_auth - Run UMTS authentication command on USIM card
 * @scard: Pointer to private data from scard_init()
 * @_rand: 16-byte RAND value from HLR/AuC
 * @autn: 16-byte AUTN value from HLR/AuC
 * @res: 16-byte buffer for RES
 * @res_len: Variable that will be set to RES length
 * @ik: 16-byte buffer for IK
 * @ck: 16-byte buffer for CK
 * @auts: 14-byte buffer for AUTS
 * Returns: 0 on success, -1 on failure, or -2 if USIM reports synchronization
 * failure
 *
 * This function performs AKA authentication using USIM card and the provided
 * RAND and AUTN values from HLR/AuC. If authentication command can be
 * completed successfully, RES, IK, and CK values will be written into provided
 * buffers and res_len is set to length of received RES value. If USIM reports
 * synchronization failure, the received AUTS value will be written into auts
 * buffer. In this case, RES, IK, and CK are not valid.
 */
int scard_umts_auth(struct scard_data *scard, const unsigned char *_rand,
		    const unsigned char *autn,
		    unsigned char *res, size_t *res_len,
		    unsigned char *ik, unsigned char *ck, unsigned char *auts)
{
	unsigned char cmd[5 + 1 + AKA_RAND_LEN + 1 + AKA_AUTN_LEN] =
		{ USIM_CMD_RUN_UMTS_ALG };
	unsigned char get_resp[5] = { USIM_CMD_GET_RESPONSE };
	unsigned char resp[3], buf[64], *pos, *end;
	size_t len;
	long ret;

	if (scard == NULL)
		return -1;

	if (scard->sim_type == SCARD_GSM_SIM) {
		wpa_printf(MSG_ERROR, "SCARD: Non-USIM card - cannot do UMTS "
			   "auth");
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "SCARD: UMTS auth - RAND", _rand, AKA_RAND_LEN);
	wpa_hexdump(MSG_DEBUG, "SCARD: UMTS auth - AUTN", autn, AKA_AUTN_LEN);
	cmd[5] = AKA_RAND_LEN;
	os_memcpy(cmd + 6, _rand, AKA_RAND_LEN);
	cmd[6 + AKA_RAND_LEN] = AKA_AUTN_LEN;
	os_memcpy(cmd + 6 + AKA_RAND_LEN + 1, autn, AKA_AUTN_LEN);

	len = sizeof(resp);
	ret = scard_transmit(scard, cmd, sizeof(cmd), resp, &len);
	if (ret != SCARD_S_SUCCESS)
		return -1;

	if (len <= sizeof(resp))
		wpa_hexdump(MSG_DEBUG, "SCARD: UMTS alg response", resp, len);

	if (len == 2 && resp[0] == 0x98 && resp[1] == 0x62) {
		wpa_printf(MSG_WARNING, "SCARD: UMTS auth failed - "
			   "MAC != XMAC");
		return -1;
	} else if (len != 2 || resp[0] != 0x61) {
		wpa_printf(MSG_WARNING, "SCARD: unexpected response for UMTS "
			   "auth request (len=%ld resp=%02x %02x)",
			   (long) len, resp[0], resp[1]);
		return -1;
	}
	get_resp[4] = resp[1];

	len = sizeof(buf);
	ret = scard_transmit(scard, get_resp, sizeof(get_resp), buf, &len);
	if (ret != SCARD_S_SUCCESS || len > sizeof(buf))
		return -1;

	wpa_hexdump(MSG_DEBUG, "SCARD: UMTS get response result", buf, len);
	if (len >= 2 + AKA_AUTS_LEN && buf[0] == 0xdc &&
	    buf[1] == AKA_AUTS_LEN) {
		wpa_printf(MSG_DEBUG, "SCARD: UMTS Synchronization-Failure");
		os_memcpy(auts, buf + 2, AKA_AUTS_LEN);
		wpa_hexdump(MSG_DEBUG, "SCARD: AUTS", auts, AKA_AUTS_LEN);
		return -2;
	} else if (len >= 6 + IK_LEN + CK_LEN && buf[0] == 0xdb) {
		pos = buf + 1;
		end = buf + len;

		/* RES */
		if (pos[0] > RES_MAX_LEN || pos + pos[0] > end) {
			wpa_printf(MSG_DEBUG, "SCARD: Invalid RES");
			return -1;
		}
		*res_len = *pos++;
		os_memcpy(res, pos, *res_len);
		pos += *res_len;
		wpa_hexdump(MSG_DEBUG, "SCARD: RES", res, *res_len);

		/* CK */
		if (pos[0] != CK_LEN || pos + CK_LEN > end) {
			wpa_printf(MSG_DEBUG, "SCARD: Invalid CK");
			return -1;
		}
		pos++;
		os_memcpy(ck, pos, CK_LEN);
		pos += CK_LEN;
		wpa_hexdump(MSG_DEBUG, "SCARD: CK", ck, CK_LEN);

		/* IK */
		if (pos[0] != IK_LEN || pos + IK_LEN > end) {
			wpa_printf(MSG_DEBUG, "SCARD: Invalid IK");
			return -1;
		}
		pos++;
		os_memcpy(ik, pos, IK_LEN);
		pos += IK_LEN;
		wpa_hexdump(MSG_DEBUG, "SCARD: IK", ik, IK_LEN);

		return 0;
	}

	wpa_printf(MSG_DEBUG, "SCARD: Unrecognized response");
	return -1;
}
