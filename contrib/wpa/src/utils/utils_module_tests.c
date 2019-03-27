/*
 * utils module tests
 * Copyright (c) 2014-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "utils/bitfield.h"
#include "utils/ext_password.h"
#include "utils/trace.h"
#include "utils/base64.h"
#include "utils/ip_addr.h"
#include "utils/eloop.h"
#include "utils/json.h"
#include "utils/module_tests.h"


struct printf_test_data {
	u8 *data;
	size_t len;
	char *encoded;
};

static const struct printf_test_data printf_tests[] = {
	{ (u8 *) "abcde", 5, "abcde" },
	{ (u8 *) "a\0b\nc\ed\re\tf\"\\", 13, "a\\0b\\nc\\ed\\re\\tf\\\"\\\\" },
	{ (u8 *) "\x00\x31\x00\x32\x00\x39", 6, "\\x001\\0002\\09" },
	{ (u8 *) "\n\n\n", 3, "\n\12\x0a" },
	{ (u8 *) "\303\245\303\244\303\266\303\205\303\204\303\226", 12,
	  "\\xc3\\xa5\xc3\\xa4\\xc3\\xb6\\xc3\\x85\\xc3\\x84\\xc3\\x96" },
	{ (u8 *) "\303\245\303\244\303\266\303\205\303\204\303\226", 12,
	  "\\303\\245\\303\\244\\303\\266\\303\\205\\303\\204\\303\\226" },
	{ (u8 *) "\xe5\xe4\xf6\xc5\xc4\xd6", 6,
	  "\\xe5\\xe4\\xf6\\xc5\\xc4\\xd6" },
	{ NULL, 0, NULL }
};


static int printf_encode_decode_tests(void)
{
	int i;
	size_t binlen;
	char buf[100];
	u8 bin[100];
	int errors = 0;
	int array[10];

	wpa_printf(MSG_INFO, "printf encode/decode tests");

	for (i = 0; printf_tests[i].data; i++) {
		const struct printf_test_data *test = &printf_tests[i];
		printf_encode(buf, sizeof(buf), test->data, test->len);
		wpa_printf(MSG_INFO, "%d: -> \"%s\"", i, buf);

		binlen = printf_decode(bin, sizeof(bin), buf);
		if (binlen != test->len ||
		    os_memcmp(bin, test->data, binlen) != 0) {
			wpa_hexdump(MSG_ERROR, "Error in decoding#1",
				    bin, binlen);
			errors++;
		}

		binlen = printf_decode(bin, sizeof(bin), test->encoded);
		if (binlen != test->len ||
		    os_memcmp(bin, test->data, binlen) != 0) {
			wpa_hexdump(MSG_ERROR, "Error in decoding#2",
				    bin, binlen);
			errors++;
		}
	}

	buf[5] = 'A';
	printf_encode(buf, 5, (const u8 *) "abcde", 5);
	if (buf[5] != 'A') {
		wpa_printf(MSG_ERROR, "Error in bounds checking#1");
		errors++;
	}

	for (i = 5; i < 10; i++) {
		buf[i] = 'A';
		printf_encode(buf, i, (const u8 *) "\xdd\xdd\xdd\xdd\xdd", 5);
		if (buf[i] != 'A') {
			wpa_printf(MSG_ERROR, "Error in bounds checking#2(%d)",
				   i);
			errors++;
		}
	}

	if (printf_decode(bin, 3, "abcde") != 2)
		errors++;

	if (printf_decode(bin, 3, "\\xa") != 1 || bin[0] != 10)
		errors++;

	if (printf_decode(bin, 3, "\\xq") != 1 || bin[0] != 'q')
		errors++;

	if (printf_decode(bin, 3, "\\a") != 1 || bin[0] != 'a')
		errors++;

	array[0] = 10;
	array[1] = 10;
	array[2] = 5;
	array[3] = 10;
	array[4] = 5;
	array[5] = 0;
	if (int_array_len(array) != 5)
		errors++;
	int_array_sort_unique(array);
	if (int_array_len(array) != 2)
		errors++;

	if (errors) {
		wpa_printf(MSG_ERROR, "%d printf test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int bitfield_tests(void)
{
	struct bitfield *bf;
	int i;
	int errors = 0;

	wpa_printf(MSG_INFO, "bitfield tests");

	bf = bitfield_alloc(123);
	if (bf == NULL)
		return -1;

	for (i = 0; i < 123; i++) {
		if (bitfield_is_set(bf, i) || bitfield_is_set(bf, i + 1))
			errors++;
		if (i > 0 && bitfield_is_set(bf, i - 1))
			errors++;
		bitfield_set(bf, i);
		if (!bitfield_is_set(bf, i))
			errors++;
		bitfield_clear(bf, i);
		if (bitfield_is_set(bf, i))
			errors++;
	}

	for (i = 123; i < 200; i++) {
		if (bitfield_is_set(bf, i) || bitfield_is_set(bf, i + 1))
			errors++;
		if (i > 0 && bitfield_is_set(bf, i - 1))
			errors++;
		bitfield_set(bf, i);
		if (bitfield_is_set(bf, i))
			errors++;
		bitfield_clear(bf, i);
		if (bitfield_is_set(bf, i))
			errors++;
	}

	for (i = 0; i < 123; i++) {
		if (bitfield_is_set(bf, i) || bitfield_is_set(bf, i + 1))
			errors++;
		bitfield_set(bf, i);
		if (!bitfield_is_set(bf, i))
			errors++;
	}

	for (i = 0; i < 123; i++) {
		if (!bitfield_is_set(bf, i))
			errors++;
		bitfield_clear(bf, i);
		if (bitfield_is_set(bf, i))
			errors++;
	}

	for (i = 0; i < 123; i++) {
		if (bitfield_get_first_zero(bf) != i)
			errors++;
		bitfield_set(bf, i);
	}
	if (bitfield_get_first_zero(bf) != -1)
		errors++;
	for (i = 0; i < 123; i++) {
		if (!bitfield_is_set(bf, i))
			errors++;
		bitfield_clear(bf, i);
		if (bitfield_get_first_zero(bf) != i)
			errors++;
		bitfield_set(bf, i);
	}
	if (bitfield_get_first_zero(bf) != -1)
		errors++;

	bitfield_free(bf);

	bf = bitfield_alloc(8);
	if (bf == NULL)
		return -1;
	if (bitfield_get_first_zero(bf) != 0)
		errors++;
	for (i = 0; i < 8; i++)
		bitfield_set(bf, i);
	if (bitfield_get_first_zero(bf) != -1)
		errors++;
	bitfield_free(bf);

	if (errors) {
		wpa_printf(MSG_ERROR, "%d bitfield test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int int_array_tests(void)
{
	int test1[] = { 1, 2, 3, 4, 5, 6, 0 };
	int test2[] = { 1, -1, 0 };
	int test3[] = { 1, 1, 1, -1, 2, 3, 4, 1, 2, 0 };
	int test3_res[] = { -1, 1, 2, 3, 4, 0 };
	int errors = 0;
	int len;

	wpa_printf(MSG_INFO, "int_array tests");

	if (int_array_len(test1) != 6 ||
	    int_array_len(test2) != 2)
		errors++;

	int_array_sort_unique(test3);
	len = int_array_len(test3_res);
	if (int_array_len(test3) != len)
		errors++;
	else if (os_memcmp(test3, test3_res, len * sizeof(int)) != 0)
		errors++;

	if (errors) {
		wpa_printf(MSG_ERROR, "%d int_array test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int ext_password_tests(void)
{
	struct ext_password_data *data;
	int ret = 0;
	struct wpabuf *pw;

	wpa_printf(MSG_INFO, "ext_password tests");

	data = ext_password_init("unknown", "foo");
	if (data != NULL)
		return -1;

	data = ext_password_init("test", NULL);
	if (data == NULL)
		return -1;
	pw = ext_password_get(data, "foo");
	if (pw != NULL)
		ret = -1;
	ext_password_free(pw);

	ext_password_deinit(data);

	pw = ext_password_get(NULL, "foo");
	if (pw != NULL)
		ret = -1;
	ext_password_free(pw);

	return ret;
}


static int trace_tests(void)
{
	wpa_printf(MSG_INFO, "trace tests");

	wpa_trace_show("test backtrace");
	wpa_trace_dump_funcname("test funcname", trace_tests);

	return 0;
}


static int base64_tests(void)
{
	int errors = 0;
	unsigned char *res;
	size_t res_len;

	wpa_printf(MSG_INFO, "base64 tests");

	res = base64_encode((const unsigned char *) "", ~0, &res_len);
	if (res) {
		errors++;
		os_free(res);
	}

	res = base64_encode((const unsigned char *) "=", 1, &res_len);
	if (!res || res_len != 5 || res[0] != 'P' || res[1] != 'Q' ||
	    res[2] != '=' || res[3] != '=' || res[4] != '\n')
		errors++;
	os_free(res);

	res = base64_encode((const unsigned char *) "=", 1, NULL);
	if (!res || res[0] != 'P' || res[1] != 'Q' ||
	    res[2] != '=' || res[3] != '=' || res[4] != '\n')
		errors++;
	os_free(res);

	res = base64_decode((const unsigned char *) "", 0, &res_len);
	if (res) {
		errors++;
		os_free(res);
	}

	res = base64_decode((const unsigned char *) "a", 1, &res_len);
	if (res) {
		errors++;
		os_free(res);
	}

	res = base64_decode((const unsigned char *) "====", 4, &res_len);
	if (res) {
		errors++;
		os_free(res);
	}

	res = base64_decode((const unsigned char *) "PQ==", 4, &res_len);
	if (!res || res_len != 1 || res[0] != '=')
		errors++;
	os_free(res);

	res = base64_decode((const unsigned char *) "P.Q-=!=*", 8, &res_len);
	if (!res || res_len != 1 || res[0] != '=')
		errors++;
	os_free(res);

	if (errors) {
		wpa_printf(MSG_ERROR, "%d base64 test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int common_tests(void)
{
	char buf[3], longbuf[100];
	u8 addr[ETH_ALEN] = { 1, 2, 3, 4, 5, 6 };
	u8 bin[3];
	int errors = 0;
	struct wpa_freq_range_list ranges;
	size_t len;
	const char *txt;
	u8 ssid[255];

	wpa_printf(MSG_INFO, "common tests");

	if (hwaddr_mask_txt(buf, 3, addr, addr) != -1)
		errors++;

	if (wpa_scnprintf(buf, 0, "hello") != 0 ||
	    wpa_scnprintf(buf, 3, "hello") != 2)
		errors++;

	if (wpa_snprintf_hex(buf, 0, addr, ETH_ALEN) != 0 ||
	    wpa_snprintf_hex(buf, 3, addr, ETH_ALEN) != 2)
		errors++;

	if (merge_byte_arrays(bin, 3, addr, ETH_ALEN, NULL, 0) != 3 ||
	    merge_byte_arrays(bin, 3, NULL, 0, addr, ETH_ALEN) != 3)
		errors++;

	if (dup_binstr(NULL, 0) != NULL)
		errors++;

	if (freq_range_list_includes(NULL, 0) != 0)
		errors++;

	os_memset(&ranges, 0, sizeof(ranges));
	if (freq_range_list_parse(&ranges, "") != 0 ||
	    freq_range_list_includes(&ranges, 0) != 0 ||
	    freq_range_list_str(&ranges) != NULL)
		errors++;

	if (utf8_unescape(NULL, 0, buf, sizeof(buf)) != 0 ||
	    utf8_unescape("a", 1, NULL, 0) != 0 ||
	    utf8_unescape("a\\", 2, buf, sizeof(buf)) != 0 ||
	    utf8_unescape("abcde", 5, buf, sizeof(buf)) != 0 ||
	    utf8_unescape("abc", 3, buf, 3) != 3)
		errors++;

	if (utf8_unescape("a", 0, buf, sizeof(buf)) != 1 || buf[0] != 'a')
		errors++;

	if (utf8_unescape("\\b", 2, buf, sizeof(buf)) != 1 || buf[0] != 'b')
		errors++;

	if (utf8_escape(NULL, 0, buf, sizeof(buf)) != 0 ||
	    utf8_escape("a", 1, NULL, 0) != 0 ||
	    utf8_escape("abcde", 5, buf, sizeof(buf)) != 0 ||
	    utf8_escape("a\\bcde", 6, buf, sizeof(buf)) != 0 ||
	    utf8_escape("ab\\cde", 6, buf, sizeof(buf)) != 0 ||
	    utf8_escape("abc\\de", 6, buf, sizeof(buf)) != 0 ||
	    utf8_escape("abc", 3, buf, 3) != 3)
		errors++;

	if (utf8_escape("a", 0, buf, sizeof(buf)) != 1 || buf[0] != 'a')
		errors++;

	os_memset(ssid, 0, sizeof(ssid));
	txt = wpa_ssid_txt(ssid, sizeof(ssid));
	len = os_strlen(txt);
	/* Verify that SSID_MAX_LEN * 4 buffer limit is enforced. */
	if (len != SSID_MAX_LEN * 4) {
		wpa_printf(MSG_ERROR,
			   "Unexpected wpa_ssid_txt() result with too long SSID");
		errors++;
	}

	if (wpa_snprintf_hex_sep(longbuf, 0, addr, ETH_ALEN, '-') != 0 ||
	    wpa_snprintf_hex_sep(longbuf, 5, addr, ETH_ALEN, '-') != 3 ||
	    os_strcmp(longbuf, "01-0") != 0)
		errors++;

	if (errors) {
		wpa_printf(MSG_ERROR, "%d common test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int os_tests(void)
{
	int errors = 0;
	void *ptr;
	os_time_t t;

	wpa_printf(MSG_INFO, "os tests");

	ptr = os_calloc((size_t) -1, (size_t) -1);
	if (ptr) {
		errors++;
		os_free(ptr);
	}
	ptr = os_calloc((size_t) 2, (size_t) -1);
	if (ptr) {
		errors++;
		os_free(ptr);
	}
	ptr = os_calloc((size_t) -1, (size_t) 2);
	if (ptr) {
		errors++;
		os_free(ptr);
	}

	ptr = os_realloc_array(NULL, (size_t) -1, (size_t) -1);
	if (ptr) {
		errors++;
		os_free(ptr);
	}

	os_sleep(1, 1);

	if (os_mktime(1969, 1, 1, 1, 1, 1, &t) == 0 ||
	    os_mktime(1971, 0, 1, 1, 1, 1, &t) == 0 ||
	    os_mktime(1971, 13, 1, 1, 1, 1, &t) == 0 ||
	    os_mktime(1971, 1, 0, 1, 1, 1, &t) == 0 ||
	    os_mktime(1971, 1, 32, 1, 1, 1, &t) == 0 ||
	    os_mktime(1971, 1, 1, -1, 1, 1, &t) == 0 ||
	    os_mktime(1971, 1, 1, 24, 1, 1, &t) == 0 ||
	    os_mktime(1971, 1, 1, 1, -1, 1, &t) == 0 ||
	    os_mktime(1971, 1, 1, 1, 60, 1, &t) == 0 ||
	    os_mktime(1971, 1, 1, 1, 1, -1, &t) == 0 ||
	    os_mktime(1971, 1, 1, 1, 1, 61, &t) == 0 ||
	    os_mktime(1971, 1, 1, 1, 1, 1, &t) != 0 ||
	    os_mktime(2020, 1, 2, 3, 4, 5, &t) != 0 ||
	    os_mktime(2015, 12, 31, 23, 59, 59, &t) != 0)
		errors++;

	if (os_setenv("hwsim_test_env", "test value", 0) != 0 ||
	    os_setenv("hwsim_test_env", "test value 2", 1) != 0 ||
	    os_unsetenv("hwsim_test_env") != 0)
		errors++;

	if (os_file_exists("/this-file-does-not-exists-hwsim") != 0)
		errors++;

	if (errors) {
		wpa_printf(MSG_ERROR, "%d os test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int wpabuf_tests(void)
{
	int errors = 0;
	void *ptr;
	struct wpabuf *buf;

	wpa_printf(MSG_INFO, "wpabuf tests");

	ptr = os_malloc(100);
	if (ptr) {
		buf = wpabuf_alloc_ext_data(ptr, 100);
		if (buf) {
			if (wpabuf_resize(&buf, 100) < 0)
				errors++;
			else
				wpabuf_put(buf, 100);
			wpabuf_free(buf);
		} else {
			errors++;
			os_free(ptr);
		}
	} else {
		errors++;
	}

	buf = wpabuf_alloc(100);
	if (buf) {
		struct wpabuf *buf2;

		wpabuf_put(buf, 100);
		if (wpabuf_resize(&buf, 100) < 0)
			errors++;
		else
			wpabuf_put(buf, 100);
		buf2 = wpabuf_concat(buf, NULL);
		if (buf2 != buf)
			errors++;
		wpabuf_free(buf2);
	} else {
		errors++;
	}

	buf = NULL;
	buf = wpabuf_zeropad(buf, 10);
	if (buf != NULL)
		errors++;

	if (errors) {
		wpa_printf(MSG_ERROR, "%d wpabuf test(s) failed", errors);
		return -1;
	}

	return 0;
}


static int ip_addr_tests(void)
{
	int errors = 0;
	struct hostapd_ip_addr addr;
	char buf[100];

	wpa_printf(MSG_INFO, "ip_addr tests");

	if (hostapd_parse_ip_addr("1.2.3.4", &addr) != 0 ||
	    addr.af != AF_INET ||
	    hostapd_ip_txt(NULL, buf, sizeof(buf)) != NULL ||
	    hostapd_ip_txt(&addr, buf, 1) != buf || buf[0] != '\0' ||
	    hostapd_ip_txt(&addr, buf, 0) != NULL ||
	    hostapd_ip_txt(&addr, buf, sizeof(buf)) != buf)
		errors++;

	if (hostapd_parse_ip_addr("::", &addr) != 0 ||
	    addr.af != AF_INET6 ||
	    hostapd_ip_txt(&addr, buf, 1) != buf || buf[0] != '\0' ||
	    hostapd_ip_txt(&addr, buf, sizeof(buf)) != buf)
		errors++;

	if (errors) {
		wpa_printf(MSG_ERROR, "%d ip_addr test(s) failed", errors);
		return -1;
	}

	return 0;
}


struct test_eloop {
	unsigned int magic;
	int close_in_timeout;
	int pipefd1[2];
	int pipefd2[2];
};


static void eloop_tests_start(int close_in_timeout);


static void eloop_test_read_2(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct test_eloop *t = eloop_ctx;
	ssize_t res;
	char buf[10];

	wpa_printf(MSG_INFO, "%s: sock=%d", __func__, sock);

	if (t->magic != 0x12345678) {
		wpa_printf(MSG_INFO, "%s: unexpected magic 0x%x",
			   __func__, t->magic);
	}

	if (t->pipefd2[0] != sock) {
		wpa_printf(MSG_INFO, "%s: unexpected sock %d != %d",
			   __func__, sock, t->pipefd2[0]);
	}

	res = read(sock, buf, sizeof(buf));
	wpa_printf(MSG_INFO, "%s: sock=%d --> res=%d",
		   __func__, sock, (int) res);
}


static void eloop_test_read_2_wrong(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct test_eloop *t = eloop_ctx;

	wpa_printf(MSG_INFO, "%s: sock=%d", __func__, sock);

	if (t->magic != 0x12345678) {
		wpa_printf(MSG_INFO, "%s: unexpected magic 0x%x",
			   __func__, t->magic);
	}

	if (t->pipefd2[0] != sock) {
		wpa_printf(MSG_INFO, "%s: unexpected sock %d != %d",
			   __func__, sock, t->pipefd2[0]);
	}

	/*
	 * This is expected to block due to the original socket with data having
	 * been closed and no new data having been written to the new socket
	 * with the same fd. To avoid blocking the process during test, skip the
	 * read here.
	 */
	wpa_printf(MSG_ERROR, "%s: FAIL - should not have called this function",
		   __func__);
}


static void reopen_pipefd2(struct test_eloop *t)
{
	if (t->pipefd2[0] < 0) {
		wpa_printf(MSG_INFO, "pipefd2 had been closed");
	} else {
		int res;

		wpa_printf(MSG_INFO, "close pipefd2");
		eloop_unregister_read_sock(t->pipefd2[0]);
		close(t->pipefd2[0]);
		t->pipefd2[0] = -1;
		close(t->pipefd2[1]);
		t->pipefd2[1] = -1;

		res = pipe(t->pipefd2);
		if (res < 0) {
			wpa_printf(MSG_INFO, "pipe: %s", strerror(errno));
			t->pipefd2[0] = -1;
			t->pipefd2[1] = -1;
			return;
		}

		wpa_printf(MSG_INFO,
			   "re-register pipefd2 with new sockets %d,%d",
			   t->pipefd2[0], t->pipefd2[1]);
		eloop_register_read_sock(t->pipefd2[0], eloop_test_read_2_wrong,
					 t, NULL);
	}
}


static void eloop_test_read_1(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct test_eloop *t = eloop_ctx;
	ssize_t res;
	char buf[10];

	wpa_printf(MSG_INFO, "%s: sock=%d", __func__, sock);

	if (t->magic != 0x12345678) {
		wpa_printf(MSG_INFO, "%s: unexpected magic 0x%x",
			   __func__, t->magic);
	}

	if (t->pipefd1[0] != sock) {
		wpa_printf(MSG_INFO, "%s: unexpected sock %d != %d",
			   __func__, sock, t->pipefd1[0]);
	}

	res = read(sock, buf, sizeof(buf));
	wpa_printf(MSG_INFO, "%s: sock=%d --> res=%d",
		   __func__, sock, (int) res);

	if (!t->close_in_timeout)
		reopen_pipefd2(t);
}


static void eloop_test_cb(void *eloop_data, void *user_ctx)
{
	struct test_eloop *t = eloop_data;

	wpa_printf(MSG_INFO, "%s", __func__);

	if (t->magic != 0x12345678) {
		wpa_printf(MSG_INFO, "%s: unexpected magic 0x%x",
			   __func__, t->magic);
	}

	if (t->close_in_timeout)
		reopen_pipefd2(t);
}


static void eloop_test_timeout(void *eloop_data, void *user_ctx)
{
	struct test_eloop *t = eloop_data;
	int next_run = 0;

	wpa_printf(MSG_INFO, "%s", __func__);

	if (t->magic != 0x12345678) {
		wpa_printf(MSG_INFO, "%s: unexpected magic 0x%x",
			   __func__, t->magic);
	}

	if (t->pipefd1[0] >= 0) {
		wpa_printf(MSG_INFO, "pipefd1 had not been closed");
		eloop_unregister_read_sock(t->pipefd1[0]);
		close(t->pipefd1[0]);
		t->pipefd1[0] = -1;
		close(t->pipefd1[1]);
		t->pipefd1[1] = -1;
	}

	if (t->pipefd2[0] >= 0) {
		wpa_printf(MSG_INFO, "pipefd2 had not been closed");
		eloop_unregister_read_sock(t->pipefd2[0]);
		close(t->pipefd2[0]);
		t->pipefd2[0] = -1;
		close(t->pipefd2[1]);
		t->pipefd2[1] = -1;
	}

	next_run = t->close_in_timeout;
	t->magic = 0;
	wpa_printf(MSG_INFO, "%s - free(%p)", __func__, t);
	os_free(t);

	if (next_run)
		eloop_tests_start(0);
}


static void eloop_tests_start(int close_in_timeout)
{
	struct test_eloop *t;
	int res;

	t = os_zalloc(sizeof(*t));
	if (!t)
		return;
	t->magic = 0x12345678;
	t->close_in_timeout = close_in_timeout;

	wpa_printf(MSG_INFO, "starting eloop tests (%p) (close_in_timeout=%d)",
		   t, close_in_timeout);

	res = pipe(t->pipefd1);
	if (res < 0) {
		wpa_printf(MSG_INFO, "pipe: %s", strerror(errno));
		os_free(t);
		return;
	}

	res = pipe(t->pipefd2);
	if (res < 0) {
		wpa_printf(MSG_INFO, "pipe: %s", strerror(errno));
		close(t->pipefd1[0]);
		close(t->pipefd1[1]);
		os_free(t);
		return;
	}

	wpa_printf(MSG_INFO, "pipe fds: %d,%d %d,%d",
		   t->pipefd1[0], t->pipefd1[1],
		   t->pipefd2[0], t->pipefd2[1]);

	eloop_register_read_sock(t->pipefd1[0], eloop_test_read_1, t, NULL);
	eloop_register_read_sock(t->pipefd2[0], eloop_test_read_2, t, NULL);
	eloop_register_timeout(0, 0, eloop_test_cb, t, NULL);
	eloop_register_timeout(0, 200000, eloop_test_timeout, t, NULL);

	if (write(t->pipefd1[1], "HELLO", 5) < 0)
		wpa_printf(MSG_INFO, "write: %s", strerror(errno));
	if (write(t->pipefd2[1], "TEST", 4) < 0)
		wpa_printf(MSG_INFO, "write: %s", strerror(errno));
	os_sleep(0, 50000);
	wpa_printf(MSG_INFO, "waiting for eloop callbacks");
}


static void eloop_tests_run(void *eloop_data, void *user_ctx)
{
	eloop_tests_start(1);
}


static int eloop_tests(void)
{
	wpa_printf(MSG_INFO, "schedule eloop tests to be run");

	/*
	 * Cannot return error from these without a significant design change,
	 * so for now, run the tests from a scheduled timeout and require
	 * separate verification of the results from the debug log.
	 */
	eloop_register_timeout(0, 0, eloop_tests_run, NULL, NULL);

	return 0;
}


#ifdef CONFIG_JSON
struct json_test_data {
	const char *json;
	const char *tree;
};

static const struct json_test_data json_test_cases[] = {
	{ "{}", "[1:OBJECT:]" },
	{ "[]", "[1:ARRAY:]" },
	{ "{", NULL },
	{ "[", NULL },
	{ "}", NULL },
	{ "]", NULL },
	{ "[[]]", "[1:ARRAY:][2:ARRAY:]" },
	{ "{\"t\":\"test\"}", "[1:OBJECT:][2:STRING:t]" },
	{ "{\"t\":123}", "[1:OBJECT:][2:NUMBER:t]" },
	{ "{\"t\":true}", "[1:OBJECT:][2:BOOLEAN:t]" },
	{ "{\"t\":false}", "[1:OBJECT:][2:BOOLEAN:t]" },
	{ "{\"t\":null}", "[1:OBJECT:][2:NULL:t]" },
	{ "{\"t\":truetrue}", NULL },
	{ "\"test\"", "[1:STRING:]" },
	{ "123", "[1:NUMBER:]" },
	{ "true", "[1:BOOLEAN:]" },
	{ "false", "[1:BOOLEAN:]" },
	{ "null", "[1:NULL:]" },
	{ "truetrue", NULL },
	{ " {\t\n\r\"a\"\n:\r1\n,\n\"b\":3\n}\n",
	  "[1:OBJECT:][2:NUMBER:a][2:NUMBER:b]" },
	{ ",", NULL },
	{ "{,}", NULL },
	{ "[,]", NULL },
	{ ":", NULL },
	{ "{:}", NULL },
	{ "[:]", NULL },
	{ "{ \"\\u005c\" : \"\\u005c\" }", "[1:OBJECT:][2:STRING:\\]" },
	{ "[{},{}]", "[1:ARRAY:][2:OBJECT:][2:OBJECT:]" },
	{ "[1,2]", "[1:ARRAY:][2:NUMBER:][2:NUMBER:]" },
	{ "[\"1\",\"2\"]", "[1:ARRAY:][2:STRING:][2:STRING:]" },
	{ "[true,false]", "[1:ARRAY:][2:BOOLEAN:][2:BOOLEAN:]" },
};
#endif /* CONFIG_JSON */


static int json_tests(void)
{
#ifdef CONFIG_JSON
	unsigned int i;
	struct json_token *root;
	char buf[1000];

	wpa_printf(MSG_INFO, "JSON tests");

	for (i = 0; i < ARRAY_SIZE(json_test_cases); i++) {
		const struct json_test_data *test = &json_test_cases[i];
		int res = 0;

		root = json_parse(test->json, os_strlen(test->json));
		if ((root && !test->tree) || (!root && test->tree)) {
			wpa_printf(MSG_INFO, "JSON test %u failed", i);
			res = -1;
		} else if (root) {
			json_print_tree(root, buf, sizeof(buf));
			if (os_strcmp(buf, test->tree) != 0) {
				wpa_printf(MSG_INFO,
					   "JSON test %u tree mismatch: %s %s",
					   i, buf, test->tree);
				res = -1;
			}
		}
		json_free(root);
		if (res < 0)
			return -1;

	}
#endif /* CONFIG_JSON */
	return 0;
}


int utils_module_tests(void)
{
	int ret = 0;

	wpa_printf(MSG_INFO, "utils module tests");

	if (printf_encode_decode_tests() < 0 ||
	    ext_password_tests() < 0 ||
	    trace_tests() < 0 ||
	    bitfield_tests() < 0 ||
	    base64_tests() < 0 ||
	    common_tests() < 0 ||
	    os_tests() < 0 ||
	    wpabuf_tests() < 0 ||
	    ip_addr_tests() < 0 ||
	    eloop_tests() < 0 ||
	    json_tests() < 0 ||
	    int_array_tests() < 0)
		ret = -1;

	return ret;
}
