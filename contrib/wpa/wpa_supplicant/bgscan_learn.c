/*
 * WPA Supplicant - background scan and roaming module: learn
 * Copyright (c) 2009-2010, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "list.h"
#include "common/ieee802_11_defs.h"
#include "drivers/driver.h"
#include "config_ssid.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "scan.h"
#include "bgscan.h"

struct bgscan_learn_bss {
	struct dl_list list;
	u8 bssid[ETH_ALEN];
	int freq;
	u8 *neigh; /* num_neigh * ETH_ALEN buffer */
	size_t num_neigh;
};

struct bgscan_learn_data {
	struct wpa_supplicant *wpa_s;
	const struct wpa_ssid *ssid;
	int scan_interval;
	int signal_threshold;
	int short_interval; /* use if signal < threshold */
	int long_interval; /* use if signal > threshold */
	struct os_reltime last_bgscan;
	char *fname;
	struct dl_list bss;
	int *supp_freqs;
	int probe_idx;
};


static void bss_free(struct bgscan_learn_bss *bss)
{
	os_free(bss->neigh);
	os_free(bss);
}


static int bssid_in_array(u8 *array, size_t array_len, const u8 *bssid)
{
	size_t i;

	if (array == NULL || array_len == 0)
		return 0;

	for (i = 0; i < array_len; i++) {
		if (os_memcmp(array + i * ETH_ALEN, bssid, ETH_ALEN) == 0)
			return 1;
	}

	return 0;
}


static void bgscan_learn_add_neighbor(struct bgscan_learn_bss *bss,
				      const u8 *bssid)
{
	u8 *n;

	if (os_memcmp(bss->bssid, bssid, ETH_ALEN) == 0)
		return;
	if (bssid_in_array(bss->neigh, bss->num_neigh, bssid))
		return;

	n = os_realloc_array(bss->neigh, bss->num_neigh + 1, ETH_ALEN);
	if (n == NULL)
		return;

	os_memcpy(n + bss->num_neigh * ETH_ALEN, bssid, ETH_ALEN);
	bss->neigh = n;
	bss->num_neigh++;
}


static struct bgscan_learn_bss * bgscan_learn_get_bss(
	struct bgscan_learn_data *data, const u8 *bssid)
{
	struct bgscan_learn_bss *bss;

	dl_list_for_each(bss, &data->bss, struct bgscan_learn_bss, list) {
		if (os_memcmp(bss->bssid, bssid, ETH_ALEN) == 0)
			return bss;
	}
	return NULL;
}


static int bgscan_learn_load(struct bgscan_learn_data *data)
{
	FILE *f;
	char buf[128];
	struct bgscan_learn_bss *bss;

	if (data->fname == NULL)
		return 0;

	f = fopen(data->fname, "r");
	if (f == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "bgscan learn: Loading data from %s",
		   data->fname);

	if (fgets(buf, sizeof(buf), f) == NULL ||
	    os_strncmp(buf, "wpa_supplicant-bgscan-learn\n", 28) != 0) {
		wpa_printf(MSG_INFO, "bgscan learn: Invalid data file %s",
			   data->fname);
		fclose(f);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		if (os_strncmp(buf, "BSS ", 4) == 0) {
			bss = os_zalloc(sizeof(*bss));
			if (!bss)
				continue;
			if (hwaddr_aton(buf + 4, bss->bssid) < 0) {
				bss_free(bss);
				continue;
			}
			bss->freq = atoi(buf + 4 + 18);
			dl_list_add(&data->bss, &bss->list);
			wpa_printf(MSG_DEBUG, "bgscan learn: Loaded BSS "
				   "entry: " MACSTR " freq=%d",
				   MAC2STR(bss->bssid), bss->freq);
		}

		if (os_strncmp(buf, "NEIGHBOR ", 9) == 0) {
			u8 addr[ETH_ALEN];

			if (hwaddr_aton(buf + 9, addr) < 0)
				continue;
			bss = bgscan_learn_get_bss(data, addr);
			if (bss == NULL)
				continue;
			if (hwaddr_aton(buf + 9 + 18, addr) < 0)
				continue;

			bgscan_learn_add_neighbor(bss, addr);
		}
	}

	fclose(f);
	return 0;
}


static void bgscan_learn_save(struct bgscan_learn_data *data)
{
	FILE *f;
	struct bgscan_learn_bss *bss;

	if (data->fname == NULL)
		return;

	wpa_printf(MSG_DEBUG, "bgscan learn: Saving data to %s",
		   data->fname);

	f = fopen(data->fname, "w");
	if (f == NULL)
		return;
	fprintf(f, "wpa_supplicant-bgscan-learn\n");

	dl_list_for_each(bss, &data->bss, struct bgscan_learn_bss, list) {
		fprintf(f, "BSS " MACSTR " %d\n",
			MAC2STR(bss->bssid), bss->freq);
	}

	dl_list_for_each(bss, &data->bss, struct bgscan_learn_bss, list) {
		size_t i;
		for (i = 0; i < bss->num_neigh; i++) {
			fprintf(f, "NEIGHBOR " MACSTR " " MACSTR "\n",
				MAC2STR(bss->bssid),
				MAC2STR(bss->neigh + i * ETH_ALEN));
		}
	}

	fclose(f);
}


static int in_array(int *array, int val)
{
	int i;

	if (array == NULL)
		return 0;

	for (i = 0; array[i]; i++) {
		if (array[i] == val)
			return 1;
	}

	return 0;
}


static int * bgscan_learn_get_freqs(struct bgscan_learn_data *data,
				    size_t *count)
{
	struct bgscan_learn_bss *bss;
	int *freqs = NULL, *n;

	*count = 0;

	dl_list_for_each(bss, &data->bss, struct bgscan_learn_bss, list) {
		if (in_array(freqs, bss->freq))
			continue;
		n = os_realloc_array(freqs, *count + 2, sizeof(int));
		if (n == NULL)
			return freqs;
		freqs = n;
		freqs[*count] = bss->freq;
		(*count)++;
		freqs[*count] = 0;
	}

	return freqs;
}


static int * bgscan_learn_get_probe_freq(struct bgscan_learn_data *data,
					 int *freqs, size_t count)
{
	int idx, *n;

	if (data->supp_freqs == NULL)
		return freqs;

	idx = data->probe_idx;
	do {
		if (!in_array(freqs, data->supp_freqs[idx])) {
			wpa_printf(MSG_DEBUG, "bgscan learn: Probe new freq "
				   "%u", data->supp_freqs[idx]);
			data->probe_idx = idx + 1;
			if (data->supp_freqs[data->probe_idx] == 0)
				data->probe_idx = 0;
			n = os_realloc_array(freqs, count + 2, sizeof(int));
			if (n == NULL)
				return freqs;
			freqs = n;
			freqs[count] = data->supp_freqs[idx];
			count++;
			freqs[count] = 0;
			break;
		}

		idx++;
		if (data->supp_freqs[idx] == 0)
			idx = 0;
	} while (idx != data->probe_idx);

	return freqs;
}


static void bgscan_learn_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct bgscan_learn_data *data = eloop_ctx;
	struct wpa_supplicant *wpa_s = data->wpa_s;
	struct wpa_driver_scan_params params;
	int *freqs = NULL;
	size_t count, i;
	char msg[100], *pos;

	os_memset(&params, 0, sizeof(params));
	params.num_ssids = 1;
	params.ssids[0].ssid = data->ssid->ssid;
	params.ssids[0].ssid_len = data->ssid->ssid_len;
	if (data->ssid->scan_freq)
		params.freqs = data->ssid->scan_freq;
	else {
		freqs = bgscan_learn_get_freqs(data, &count);
		wpa_printf(MSG_DEBUG, "bgscan learn: BSSes in this ESS have "
			   "been seen on %u channels", (unsigned int) count);
		freqs = bgscan_learn_get_probe_freq(data, freqs, count);

		msg[0] = '\0';
		pos = msg;
		for (i = 0; freqs && freqs[i]; i++) {
			int ret;
			ret = os_snprintf(pos, msg + sizeof(msg) - pos, " %d",
					  freqs[i]);
			if (os_snprintf_error(msg + sizeof(msg) - pos, ret))
				break;
			pos += ret;
		}
		pos[0] = '\0';
		wpa_printf(MSG_DEBUG, "bgscan learn: Scanning frequencies:%s",
			   msg);
		params.freqs = freqs;
	}

	wpa_printf(MSG_DEBUG, "bgscan learn: Request a background scan");
	if (wpa_supplicant_trigger_scan(wpa_s, &params)) {
		wpa_printf(MSG_DEBUG, "bgscan learn: Failed to trigger scan");
		eloop_register_timeout(data->scan_interval, 0,
				       bgscan_learn_timeout, data, NULL);
	} else
		os_get_reltime(&data->last_bgscan);
	os_free(freqs);
}


static int bgscan_learn_get_params(struct bgscan_learn_data *data,
				   const char *params)
{
	const char *pos;

	data->short_interval = atoi(params);

	pos = os_strchr(params, ':');
	if (pos == NULL)
		return 0;
	pos++;
	data->signal_threshold = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos == NULL) {
		wpa_printf(MSG_ERROR, "bgscan learn: Missing scan interval "
			   "for high signal");
		return -1;
	}
	pos++;
	data->long_interval = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos) {
		pos++;
		data->fname = os_strdup(pos);
	}

	return 0;
}


static int * bgscan_learn_get_supp_freqs(struct wpa_supplicant *wpa_s)
{
	struct hostapd_hw_modes *modes;
	int i, j, *freqs = NULL, *n;
	size_t count = 0;

	modes = wpa_s->hw.modes;
	if (modes == NULL)
		return NULL;

	for (i = 0; i < wpa_s->hw.num_modes; i++) {
		for (j = 0; j < modes[i].num_channels; j++) {
			if (modes[i].channels[j].flag & HOSTAPD_CHAN_DISABLED)
				continue;
			/* some hw modes (e.g. 11b & 11g) contain same freqs */
			if (in_array(freqs, modes[i].channels[j].freq))
				continue;
			n = os_realloc_array(freqs, count + 2, sizeof(int));
			if (n == NULL)
				continue;

			freqs = n;
			freqs[count] = modes[i].channels[j].freq;
			count++;
			freqs[count] = 0;
		}
	}

	return freqs;
}


static void * bgscan_learn_init(struct wpa_supplicant *wpa_s,
				const char *params,
				const struct wpa_ssid *ssid)
{
	struct bgscan_learn_data *data;

	data = os_zalloc(sizeof(*data));
	if (data == NULL)
		return NULL;
	dl_list_init(&data->bss);
	data->wpa_s = wpa_s;
	data->ssid = ssid;
	if (bgscan_learn_get_params(data, params) < 0) {
		os_free(data->fname);
		os_free(data);
		return NULL;
	}
	if (data->short_interval <= 0)
		data->short_interval = 30;
	if (data->long_interval <= 0)
		data->long_interval = 30;

	if (bgscan_learn_load(data) < 0) {
		os_free(data->fname);
		os_free(data);
		return NULL;
	}

	wpa_printf(MSG_DEBUG, "bgscan learn: Signal strength threshold %d  "
		   "Short bgscan interval %d  Long bgscan interval %d",
		   data->signal_threshold, data->short_interval,
		   data->long_interval);

	if (data->signal_threshold &&
	    wpa_drv_signal_monitor(wpa_s, data->signal_threshold, 4) < 0) {
		wpa_printf(MSG_ERROR, "bgscan learn: Failed to enable "
			   "signal strength monitoring");
	}

	data->supp_freqs = bgscan_learn_get_supp_freqs(wpa_s);
	data->scan_interval = data->short_interval;
	if (data->signal_threshold) {
		/* Poll for signal info to set initial scan interval */
		struct wpa_signal_info siginfo;
		if (wpa_drv_signal_poll(wpa_s, &siginfo) == 0 &&
		    siginfo.current_signal >= data->signal_threshold)
			data->scan_interval = data->long_interval;
	}

	eloop_register_timeout(data->scan_interval, 0, bgscan_learn_timeout,
			       data, NULL);

	/*
	 * This function is called immediately after an association, so it is
	 * reasonable to assume that a scan was completed recently. This makes
	 * us skip an immediate new scan in cases where the current signal
	 * level is below the bgscan threshold.
	 */
	os_get_reltime(&data->last_bgscan);

	return data;
}


static void bgscan_learn_deinit(void *priv)
{
	struct bgscan_learn_data *data = priv;
	struct bgscan_learn_bss *bss, *n;

	bgscan_learn_save(data);
	eloop_cancel_timeout(bgscan_learn_timeout, data, NULL);
	if (data->signal_threshold)
		wpa_drv_signal_monitor(data->wpa_s, 0, 0);
	os_free(data->fname);
	dl_list_for_each_safe(bss, n, &data->bss, struct bgscan_learn_bss,
			      list) {
		dl_list_del(&bss->list);
		bss_free(bss);
	}
	os_free(data->supp_freqs);
	os_free(data);
}


static int bgscan_learn_bss_match(struct bgscan_learn_data *data,
				  struct wpa_scan_res *bss)
{
	const u8 *ie;

	ie = wpa_scan_get_ie(bss, WLAN_EID_SSID);
	if (ie == NULL)
		return 0;

	if (data->ssid->ssid_len != ie[1] ||
	    os_memcmp(data->ssid->ssid, ie + 2, ie[1]) != 0)
		return 0; /* SSID mismatch */

	return 1;
}


static int bgscan_learn_notify_scan(void *priv,
				    struct wpa_scan_results *scan_res)
{
	struct bgscan_learn_data *data = priv;
	size_t i, j;
#define MAX_BSS 50
	u8 bssid[MAX_BSS * ETH_ALEN];
	size_t num_bssid = 0;

	wpa_printf(MSG_DEBUG, "bgscan learn: scan result notification");

	eloop_cancel_timeout(bgscan_learn_timeout, data, NULL);
	eloop_register_timeout(data->scan_interval, 0, bgscan_learn_timeout,
			       data, NULL);

	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *res = scan_res->res[i];
		if (!bgscan_learn_bss_match(data, res))
			continue;

		if (num_bssid < MAX_BSS) {
			os_memcpy(bssid + num_bssid * ETH_ALEN, res->bssid,
				  ETH_ALEN);
			num_bssid++;
		}
	}
	wpa_printf(MSG_DEBUG, "bgscan learn: %u matching BSSes in scan "
		   "results", (unsigned int) num_bssid);

	for (i = 0; i < scan_res->num; i++) {
		struct wpa_scan_res *res = scan_res->res[i];
		struct bgscan_learn_bss *bss;

		if (!bgscan_learn_bss_match(data, res))
			continue;

		bss = bgscan_learn_get_bss(data, res->bssid);
		if (bss && bss->freq != res->freq) {
			wpa_printf(MSG_DEBUG, "bgscan learn: Update BSS "
			   MACSTR " freq %d -> %d",
				   MAC2STR(res->bssid), bss->freq, res->freq);
			bss->freq = res->freq;
		} else if (!bss) {
			wpa_printf(MSG_DEBUG, "bgscan learn: Add BSS " MACSTR
				   " freq=%d", MAC2STR(res->bssid), res->freq);
			bss = os_zalloc(sizeof(*bss));
			if (!bss)
				continue;
			os_memcpy(bss->bssid, res->bssid, ETH_ALEN);
			bss->freq = res->freq;
			dl_list_add(&data->bss, &bss->list);
		}

		for (j = 0; j < num_bssid; j++) {
			u8 *addr = bssid + j * ETH_ALEN;
			bgscan_learn_add_neighbor(bss, addr);
		}
	}

	/*
	 * A more advanced bgscan could process scan results internally, select
	 * the BSS and request roam if needed. This sample uses the existing
	 * BSS/ESS selection routine. Change this to return 1 if selection is
	 * done inside the bgscan module.
	 */

	return 0;
}


static void bgscan_learn_notify_beacon_loss(void *priv)
{
	wpa_printf(MSG_DEBUG, "bgscan learn: beacon loss");
	/* TODO: speed up background scanning */
}


static void bgscan_learn_notify_signal_change(void *priv, int above,
					      int current_signal,
					      int current_noise,
					      int current_txrate)
{
	struct bgscan_learn_data *data = priv;
	int scan = 0;
	struct os_reltime now;

	if (data->short_interval == data->long_interval ||
	    data->signal_threshold == 0)
		return;

	wpa_printf(MSG_DEBUG, "bgscan learn: signal level changed "
		   "(above=%d current_signal=%d current_noise=%d "
		   "current_txrate=%d)", above, current_signal,
		   current_noise, current_txrate);
	if (data->scan_interval == data->long_interval && !above) {
		wpa_printf(MSG_DEBUG, "bgscan learn: Start using short bgscan "
			   "interval");
		data->scan_interval = data->short_interval;
		os_get_reltime(&now);
		if (now.sec > data->last_bgscan.sec + 1)
			scan = 1;
	} else if (data->scan_interval == data->short_interval && above) {
		wpa_printf(MSG_DEBUG, "bgscan learn: Start using long bgscan "
			   "interval");
		data->scan_interval = data->long_interval;
		eloop_cancel_timeout(bgscan_learn_timeout, data, NULL);
		eloop_register_timeout(data->scan_interval, 0,
				       bgscan_learn_timeout, data, NULL);
	} else if (!above) {
		/*
		 * Signal dropped further 4 dB. Request a new scan if we have
		 * not yet scanned in a while.
		 */
		os_get_reltime(&now);
		if (now.sec > data->last_bgscan.sec + 10)
			scan = 1;
	}

	if (scan) {
		wpa_printf(MSG_DEBUG, "bgscan learn: Trigger immediate scan");
		eloop_cancel_timeout(bgscan_learn_timeout, data, NULL);
		eloop_register_timeout(0, 0, bgscan_learn_timeout, data, NULL);
	}
}


const struct bgscan_ops bgscan_learn_ops = {
	.name = "learn",
	.init = bgscan_learn_init,
	.deinit = bgscan_learn_deinit,
	.notify_scan = bgscan_learn_notify_scan,
	.notify_beacon_loss = bgscan_learn_notify_beacon_loss,
	.notify_signal_change = bgscan_learn_notify_signal_change,
};
