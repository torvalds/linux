/*
 * Copyright (C) 1996 Paul Mackerras.
 */
#include "nonstdio.h"
#include "privinst.h"

#define scanhex	xmon_scanhex
#define skipbl	xmon_skipbl

#define ADB_B		(*(volatile unsigned char *)0xf3016000)
#define ADB_SR		(*(volatile unsigned char *)0xf3017400)
#define ADB_ACR		(*(volatile unsigned char *)0xf3017600)
#define ADB_IFR		(*(volatile unsigned char *)0xf3017a00)

static inline void eieio(void) { asm volatile ("eieio" : :); }

#define N_ADB_LOG	1000
struct adb_log {
    unsigned char b;
    unsigned char ifr;
    unsigned char acr;
    unsigned int time;
} adb_log[N_ADB_LOG];
int n_adb_log;

void
init_adb_log(void)
{
    adb_log[0].b = ADB_B;
    adb_log[0].ifr = ADB_IFR;
    adb_log[0].acr = ADB_ACR;
    adb_log[0].time = get_dec();
    n_adb_log = 0;
}

void
dump_adb_log(void)
{
    unsigned t, t0;
    struct adb_log *ap;
    int i;

    ap = adb_log;
    t0 = ap->time;
    for (i = 0; i <= n_adb_log; ++i, ++ap) {
	t = t0 - ap->time;
	printf("b=%x ifr=%x acr=%x at %d.%.7d\n", ap->b, ap->ifr, ap->acr,
	       t / 1000000000, (t % 1000000000) / 100);
    }
}

void
adb_chklog(void)
{
    struct adb_log *ap = &adb_log[n_adb_log + 1];

    ap->b = ADB_B;
    ap->ifr = ADB_IFR;
    ap->acr = ADB_ACR;
    if (ap->b != ap[-1].b || (ap->ifr & 4) != (ap[-1].ifr & 4)
	|| ap->acr != ap[-1].acr) {
	ap->time = get_dec();
	++n_adb_log;
    }
}

int
adb_bitwait(int bmask, int bval, int fmask, int fval)
{
    int i;
    struct adb_log *ap;

    for (i = 10000; i > 0; --i) {
	adb_chklog();
	ap = &adb_log[n_adb_log];
	if ((ap->b & bmask) == bval && (ap->ifr & fmask) == fval)
	    return 0;
    }
    return -1;
}

int
adb_wait(void)
{
    if (adb_bitwait(0, 0, 4, 4) < 0) {
	printf("adb: ready wait timeout\n");
	return -1;
    }
    return 0;
}

void
adb_readin(void)
{
    int i, j;
    unsigned char d[64];

    if (ADB_B & 8) {
	printf("ADB_B: %x\n", ADB_B);
	return;
    }
    i = 0;
    adb_wait();
    j = ADB_SR;
    eieio();
    ADB_B &= ~0x20;
    eieio();
    for (;;) {
	if (adb_wait() < 0)
	    break;
	d[i++] = ADB_SR;
	eieio();
	if (ADB_B & 8)
	    break;
	ADB_B ^= 0x10;
	eieio();
    }
    ADB_B |= 0x30;
    if (adb_wait() == 0)
	j = ADB_SR;
    for (j = 0; j < i; ++j)
	printf("%.2x ", d[j]);
    printf("\n");
}

int
adb_write(unsigned char *d, int i)
{
    int j;
    unsigned x;

    if ((ADB_B & 8) == 0) {
	printf("r: ");
	adb_readin();
    }
    for (;;) {
	ADB_ACR = 0x1c;
	eieio();
	ADB_SR = d[0];
	eieio();
	ADB_B &= ~0x20;
	eieio();
	if (ADB_B & 8)
	    break;
	ADB_ACR = 0xc;
	eieio();
	ADB_B |= 0x20;
	eieio();
	adb_readin();
    }
    adb_wait();
    for (j = 1; j < i; ++j) {
	ADB_SR = d[j];
	eieio();
	ADB_B ^= 0x10;
	eieio();
	if (adb_wait() < 0)
	    break;
    }
    ADB_ACR = 0xc;
    eieio();
    x = ADB_SR;
    eieio();
    ADB_B |= 0x30;
    return j;
}

void
adbcmds(void)
{
    char cmd;
    unsigned rtcu, rtcl, dec, pdec, x;
    int i, j;
    unsigned char d[64];

    cmd = skipbl();
    switch (cmd) {
    case 't':
	for (;;) {
	    rtcl = get_rtcl();
	    rtcu = get_rtcu();
	    dec = get_dec();
	    printf("rtc u=%u l=%u dec=%x (%d = %d.%.7d)\n",
		   rtcu, rtcl, dec, pdec - dec, (pdec - dec) / 1000000000,
		   ((pdec - dec) % 1000000000) / 100);
	    pdec = dec;
	    if (cmd == 'x')
		break;
	    while (xmon_read(stdin, &cmd, 1) != 1)
		;
	}
	break;
    case 'r':
	init_adb_log();
	while (adb_bitwait(8, 0, 0, 0) == 0)
	    adb_readin();
	break;
    case 'w':
	i = 0;
	while (scanhex(&x))
	    d[i++] = x;
	init_adb_log();
	j = adb_write(d, i);
	printf("sent %d bytes\n", j);
	while (adb_bitwait(8, 0, 0, 0) == 0)
	    adb_readin();
	break;
    case 'l':
	dump_adb_log();
	break;
    }
}
