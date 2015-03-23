/*
 * Random number generator
 * Copyright (c) 2010-2011, Jouni Malinen <j@w1.fi>
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
 * This random number generator is used to provide additional entropy to the
 * one provided by the operating system (os_get_random()) for session key
 * generation. The os_get_random() output is expected to be secure and the
 * implementation here is expected to provide only limited protection against
 * cases where os_get_random() cannot provide strong randomness. This
 * implementation shall not be assumed to be secure as the sole source of
 * randomness. The random_get_bytes() function mixes in randomness from
 * os_get_random() and as such, calls to os_get_random() can be replaced with
 * calls to random_get_bytes() without reducing security.
 *
 * The design here follows partially the design used in the Linux
 * drivers/char/random.c, but the implementation here is simpler and not as
 * strong. This is a compromise to reduce duplicated CPU effort and to avoid
 * extra code/memory size. As pointed out above, os_get_random() needs to be
 * guaranteed to be secure for any of the security assumptions to hold.
 */

#include "utils/includes.h"
#ifdef __linux__
#include <fcntl.h>
#endif /* __linux__ */

#include "utils/common.h"
#include "utils/eloop.h"
#include "sha1.h"
#include "random.h"

#define POOL_WORDS 32
#define POOL_WORDS_MASK (POOL_WORDS - 1)
#define POOL_TAP1 26
#define POOL_TAP2 20
#define POOL_TAP3 14
#define POOL_TAP4 7
#define POOL_TAP5 1
#define EXTRACT_LEN 16
#define MIN_READY_MARK 2

static u32 pool[POOL_WORDS];
static unsigned int input_rotate = 0;
static unsigned int pool_pos = 0;
static u8 dummy_key[20];
#ifdef __linux__
static size_t dummy_key_avail = 0;
static int random_fd = -1;
#endif /* __linux__ */
static unsigned int own_pool_ready = 0;

#define MIN_COLLECT_ENTROPY 1000
static unsigned int entropy = 0;
static unsigned int total_collected = 0;


static u32 __ROL32(u32 x, u32 y)
{
	return (x << (y & 31)) | (x >> (32 - (y & 31)));
}


static void random_mix_pool(const void *buf, size_t len)
{
	static const u32 twist[8] = {
		0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
		0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278
	};
	const u8 *pos = buf;
	u32 w;

	wpa_hexdump_key(MSG_EXCESSIVE, "random_mix_pool", buf, len);

	while (len--) {
		w = __ROL32(*pos++, input_rotate & 31);
		input_rotate += pool_pos ? 7 : 14;
		pool_pos = (pool_pos - 1) & POOL_WORDS_MASK;
		w ^= pool[pool_pos];
		w ^= pool[(pool_pos + POOL_TAP1) & POOL_WORDS_MASK];
		w ^= pool[(pool_pos + POOL_TAP2) & POOL_WORDS_MASK];
		w ^= pool[(pool_pos + POOL_TAP3) & POOL_WORDS_MASK];
		w ^= pool[(pool_pos + POOL_TAP4) & POOL_WORDS_MASK];
		w ^= pool[(pool_pos + POOL_TAP5) & POOL_WORDS_MASK];
		pool[pool_pos] = (w >> 3) ^ twist[w & 7];
	}
}


static void random_extract(u8 *out)
{
	unsigned int i;
	u8 hash[SHA1_MAC_LEN];
	u32 *hash_ptr;
	u32 buf[POOL_WORDS / 2];

	/* First, add hash back to pool to make backtracking more difficult. */
	hmac_sha1(dummy_key, sizeof(dummy_key), (const u8 *) pool,
		  sizeof(pool), hash);
	random_mix_pool(hash, sizeof(hash));
	/* Hash half the pool to extra data */
	for (i = 0; i < POOL_WORDS / 2; i++)
		buf[i] = pool[(pool_pos - i) & POOL_WORDS_MASK];
	hmac_sha1(dummy_key, sizeof(dummy_key), (const u8 *) buf,
		  sizeof(buf), hash);
	/*
	 * Fold the hash to further reduce any potential output pattern.
	 * Though, compromise this to reduce CPU use for the most common output
	 * length (32) and return 16 bytes from instead of only half.
	 */
	hash_ptr = (u32 *) hash;
	hash_ptr[0] ^= hash_ptr[4];
	os_memcpy(out, hash, EXTRACT_LEN);
}


void random_add_randomness(const void *buf, size_t len)
{
	struct os_time t;
	static unsigned int count = 0;

	count++;
	wpa_printf(MSG_MSGDUMP, "Add randomness: count=%u entropy=%u",
		   count, entropy);
	if (entropy > MIN_COLLECT_ENTROPY && (count & 0x3ff) != 0) {
		/*
		 * No need to add more entropy at this point, so save CPU and
		 * skip the update.
		 */
		return;
	}

	os_get_time(&t);
	wpa_hexdump_key(MSG_EXCESSIVE, "random pool",
			(const u8 *) pool, sizeof(pool));
	random_mix_pool(&t, sizeof(t));
	random_mix_pool(buf, len);
	wpa_hexdump_key(MSG_EXCESSIVE, "random pool",
			(const u8 *) pool, sizeof(pool));
	entropy++;
	total_collected++;
}


int random_get_bytes(void *buf, size_t len)
{
	int ret;
	u8 *bytes = buf;
	size_t left;

	wpa_printf(MSG_MSGDUMP, "Get randomness: len=%u entropy=%u",
		   (unsigned int) len, entropy);

	/* Start with assumed strong randomness from OS */
	ret = os_get_random(buf, len);
	wpa_hexdump_key(MSG_EXCESSIVE, "random from os_get_random",
			buf, len);

	/* Mix in additional entropy extracted from the internal pool */
	left = len;
	while (left) {
		size_t siz, i;
		u8 tmp[EXTRACT_LEN];
		random_extract(tmp);
		wpa_hexdump_key(MSG_EXCESSIVE, "random from internal pool",
				tmp, sizeof(tmp));
		siz = left > EXTRACT_LEN ? EXTRACT_LEN : left;
		for (i = 0; i < siz; i++)
			*bytes++ ^= tmp[i];
		left -= siz;
	}
	wpa_hexdump_key(MSG_EXCESSIVE, "mixed random", buf, len);

	if (entropy < len)
		entropy = 0;
	else
		entropy -= len;

	return ret;
}


int random_pool_ready(void)
{
#ifdef __linux__
	int fd;
	ssize_t res;

	/*
	 * Make sure that there is reasonable entropy available before allowing
	 * some key derivation operations to proceed.
	 */

	if (dummy_key_avail == sizeof(dummy_key))
		return 1; /* Already initialized - good to continue */

	/*
	 * Try to fetch some more data from the kernel high quality
	 * /dev/random. There may not be enough data available at this point,
	 * so use non-blocking read to avoid blocking the application
	 * completely.
	 */
	fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
#ifndef CONFIG_NO_STDOUT_DEBUG
		int error = errno;
		perror("open(/dev/random)");
		wpa_printf(MSG_ERROR, "random: Cannot open /dev/random: %s",
			   strerror(error));
#endif /* CONFIG_NO_STDOUT_DEBUG */
		return -1;
	}

	res = read(fd, dummy_key + dummy_key_avail,
		   sizeof(dummy_key) - dummy_key_avail);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "random: Cannot read from /dev/random: "
			   "%s", strerror(errno));
		res = 0;
	}
	wpa_printf(MSG_DEBUG, "random: Got %u/%u bytes from "
		   "/dev/random", (unsigned) res,
		   (unsigned) (sizeof(dummy_key) - dummy_key_avail));
	dummy_key_avail += res;
	close(fd);

	if (dummy_key_avail == sizeof(dummy_key))
		return 1;

	wpa_printf(MSG_INFO, "random: Only %u/%u bytes of strong "
		   "random data available from /dev/random",
		   (unsigned) dummy_key_avail, (unsigned) sizeof(dummy_key));

	if (own_pool_ready >= MIN_READY_MARK ||
	    total_collected + 10 * own_pool_ready > MIN_COLLECT_ENTROPY) {
		wpa_printf(MSG_INFO, "random: Allow operation to proceed "
			   "based on internal entropy");
		return 1;
	}

	wpa_printf(MSG_INFO, "random: Not enough entropy pool available for "
		   "secure operations");
	return 0;
#else /* __linux__ */
	/* TODO: could do similar checks on non-Linux platforms */
	return 1;
#endif /* __linux__ */
}


void random_mark_pool_ready(void)
{
	own_pool_ready++;
	wpa_printf(MSG_DEBUG, "random: Mark internal entropy pool to be "
		   "ready (count=%u/%u)", own_pool_ready, MIN_READY_MARK);
}


#ifdef __linux__

static void random_close_fd(void)
{
	if (random_fd >= 0) {
		eloop_unregister_read_sock(random_fd);
		close(random_fd);
		random_fd = -1;
	}
}


static void random_read_fd(int sock, void *eloop_ctx, void *sock_ctx)
{
	ssize_t res;

	if (dummy_key_avail == sizeof(dummy_key)) {
		random_close_fd();
		return;
	}

	res = read(sock, dummy_key + dummy_key_avail,
		   sizeof(dummy_key) - dummy_key_avail);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "random: Cannot read from /dev/random: "
			   "%s", strerror(errno));
		return;
	}

	wpa_printf(MSG_DEBUG, "random: Got %u/%u bytes from /dev/random",
		   (unsigned) res,
		   (unsigned) (sizeof(dummy_key) - dummy_key_avail));
	dummy_key_avail += res;

	if (dummy_key_avail == sizeof(dummy_key))
		random_close_fd();
}

#endif /* __linux__ */


void random_init(void)
{
#ifdef __linux__
	if (random_fd >= 0)
		return;

	random_fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (random_fd < 0) {
#ifndef CONFIG_NO_STDOUT_DEBUG
		int error = errno;
		perror("open(/dev/random)");
		wpa_printf(MSG_ERROR, "random: Cannot open /dev/random: %s",
			   strerror(error));
#endif /* CONFIG_NO_STDOUT_DEBUG */
		return;
	}
	wpa_printf(MSG_DEBUG, "random: Trying to read entropy from "
		   "/dev/random");

	eloop_register_read_sock(random_fd, random_read_fd, NULL, NULL);
#endif /* __linux__ */
}


void random_deinit(void)
{
#ifdef __linux__
	random_close_fd();
#endif /* __linux__ */
}
