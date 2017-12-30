/*  sha_speed - simple SHA benchmark tool for cryptodev
 *
 *    Copyright (C) 2011 by Phil Sutter <phil.sutter@viprinet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>

#include <crypto/cryptodev.h>

static double udifftimeval(struct timeval start, struct timeval end)
{
	return (double)(end.tv_usec - start.tv_usec) +
	       (double)(end.tv_sec - start.tv_sec) * 1000 * 1000;
}

static int must_finish = 0;

static void alarm_handler(int signo)
{
        must_finish = 1;
}

static char *units[] = { "", "Ki", "Mi", "Gi", "Ti", 0};
static char *si_units[] = { "", "K", "M", "G", "T", 0};

static void value2human(int si, double bytes, double time, double* data, double* speed,char* metric)
{
	int unit = 0;

	*data = bytes;

	if (si) {
		while (*data > 1000 && si_units[unit + 1]) {
			*data /= 1000;
			unit++;
		}
		*speed = *data / time;
		sprintf(metric, "%sB", si_units[unit]);
	} else {
		while (*data > 1024 && units[unit + 1]) {
			*data /= 1024;
			unit++;
		}
		*speed = *data / time;
		sprintf(metric, "%sB", units[unit]);
	}
}


int hash_data(struct session_op *sess, int fdc, int chunksize, int alignmask)
{
	struct crypt_op cop;
	char *buffer;
	static int val = 23;
	struct timeval start, end;
	double total = 0;
	double secs, ddata, dspeed;
	char metric[16];
	uint8_t mac[AALG_MAX_RESULT_LEN];

	if (alignmask) {
		if (posix_memalign((void **)&buffer, alignmask + 1, chunksize)) {
			printf("posix_memalign() failed!\n");
			return 1;
		}
	} else {
		if (!(buffer = malloc(chunksize))) {
			perror("malloc()");
			return 1;
		}
	}

	printf("\tEncrypting in chunks of %d bytes: ", chunksize);
	fflush(stdout);

	memset(buffer, val++, chunksize);

	must_finish = 0;
	alarm(5);

	gettimeofday(&start, NULL);
	do {
		memset(&cop, 0, sizeof(cop));
		cop.ses = sess->ses;
		cop.len = chunksize;
		cop.op = COP_ENCRYPT;
		cop.src = (unsigned char *)buffer;
		cop.mac = mac;

		if (ioctl(fdc, CIOCCRYPT, &cop)) {
			perror("ioctl(CIOCCRYPT)");
			return 1;
		}
		total+=chunksize;
	} while(must_finish==0);
	gettimeofday(&end, NULL);

	secs = udifftimeval(start, end)/ 1000000.0;

	value2human(1, total, secs, &ddata, &dspeed, metric);
	printf ("done. %.2f %s in %.2f secs: ", ddata, metric, secs);
	printf ("%.2f %s/sec\n", dspeed, metric);

	free(buffer);
	return 0;
}

int main(void)
{
	int fd, i, fdc = -1, alignmask = 0;
	struct session_op sess;
#ifdef CIOCGSESSINFO
	struct session_info_op siop;
#endif

	signal(SIGALRM, alarm_handler);

	if ((fd = open("/dev/crypto", O_RDWR, 0)) < 0) {
		perror("open()");
		return 1;
	}
	if (ioctl(fd, CRIOGET, &fdc)) {
		perror("ioctl(CRIOGET)");
		return 1;
	}

	fprintf(stderr, "Testing SHA1 Hash: \n");
	memset(&sess, 0, sizeof(sess));
	sess.mac = CRYPTO_SHA1;
	if (ioctl(fdc, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}
#ifdef CIOCGSESSINFO
	siop.ses = sess.ses;
	if (ioctl(fdc, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
	printf("requested hash CRYPTO_SHA1, got %s with driver %s\n",
			siop.hash_info.cra_name, siop.hash_info.cra_driver_name);
	alignmask = siop.alignmask;
#endif

	for (i = 256; i <= (64 * 1024); i *= 4) {
		if (hash_data(&sess, fdc, i, alignmask))
			break;
	}

	fprintf(stderr, "\nTesting SHA256 Hash: \n");
	memset(&sess, 0, sizeof(sess));
	sess.mac = CRYPTO_SHA2_256;
	if (ioctl(fdc, CIOCGSESSION, &sess)) {
		perror("ioctl(CIOCGSESSION)");
		return 1;
	}
#ifdef CIOCGSESSINFO
	siop.ses = sess.ses;
	if (ioctl(fdc, CIOCGSESSINFO, &siop)) {
		perror("ioctl(CIOCGSESSINFO)");
		return 1;
	}
	printf("requested hash CRYPTO_SHA2_256, got %s with driver %s\n",
			siop.hash_info.cra_name, siop.hash_info.cra_driver_name);
	alignmask = siop.alignmask;
#endif

	for (i = 256; i <= (64 * 1024); i *= 4) {
		if (hash_data(&sess, fdc, i, alignmask))
			break;
	}

	close(fdc);
	close(fd);
	return 0;
}
