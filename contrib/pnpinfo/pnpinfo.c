/*
 * Copyright (c) 1996, Sujal M. Patel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/time.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <machine/cpufunc.h>

#include <isa/pnpreg.h>

#ifdef DEBUG
#define	DEB(x) x
#else
#define DEB(x)
#endif
#define DDB(x) x

void
pnp_write(int d, u_char r)
{
    outb (_PNP_ADDRESS, d);
    outb (_PNP_WRITE_DATA, r);
}

/* The READ_DATA port that we are using currently */
static int rd_port;

u_char
pnp_read(int d)
{
    outb(_PNP_ADDRESS, d);
    return inb( (rd_port << 2) + 3) & 0xff;
}

u_short
pnp_readw(int d)
{
    int c = pnp_read(d) << 8 ;
    c |= pnp_read(d+1);
    return c;
}

int logdevs=0;

void DELAY __P((int i));
void send_Initiation_LFSR();
int get_serial __P((u_char *data));
int get_resource_info __P((u_char *buffer, int len));
int handle_small_res __P((u_char *resinfo, int item, int len));
void handle_large_res __P((u_char *resinfo, int item, int len));
void dump_resdata __P((u_char *data, int csn));
int isolation_protocol();


/*
 * DELAY does accurate delaying in user-space.
 * This function busy-waits.
 */
void
DELAY (int i)
{
    struct timeval t;
    long start, stop;

    i *= 4;

    gettimeofday (&t, NULL);
    start = t.tv_sec * 1000000 + t.tv_usec;
    do {
	gettimeofday (&t, NULL);
	stop = t.tv_sec * 1000000 + t.tv_usec;
    } while (start + i > stop);
}


/*
 * Send Initiation LFSR as described in "Plug and Play ISA Specification,
 * Intel May 94."
 */
void
send_Initiation_LFSR()
{
    int cur, i;

    pnp_write(PNP_CONFIG_CONTROL, 0x2);

    /* Reset the LSFR */
    outb(_PNP_ADDRESS, 0);
    outb(_PNP_ADDRESS, 0); /* yes, we do need it twice! */

    cur = 0x6a;

    for (i = 0; i < 32; i++) {
	outb(_PNP_ADDRESS, cur);
	cur = (cur >> 1) | (((cur ^ (cur >> 1)) << 7) & 0xff);
    }
}

/*
 * Get the device's serial number.  Returns 1 if the serial is valid.
 */
int
get_serial(u_char *data)
{
    int i, bit, valid = 0, sum = 0x6a;

    bzero(data, sizeof(char) * 9);

    for (i = 0; i < 72; i++) {
	bit = inb((rd_port << 2) | 0x3) == 0x55;
	DELAY(250);	/* Delay 250 usec */

	/* Can't Short Circuit the next evaluation, so 'and' is last */
	bit = (inb((rd_port << 2) | 0x3) == 0xaa) && bit;
	DELAY(250);	/* Delay 250 usec */

	valid = valid || bit;

	if (i < 64)
	    sum = (sum >> 1) |
		(((sum ^ (sum >> 1) ^ bit) << 7) & 0xff);

	data[i / 8] = (data[i / 8] >> 1) | (bit ? 0x80 : 0);
    }

    valid = valid && (data[8] == sum);

    return valid;
}


/*
 * Fill's the buffer with resource info from the device.
 * Returns 0 if the device fails to report
 */
int
get_resource_info(u_char *buffer, int len)
{
    int i, j;

    for (i = 0; i < len; i++) {
	outb(_PNP_ADDRESS, PNP_STATUS);
	for (j = 0; j < 100; j++) {
	    if ((inb((rd_port << 2) | 0x3)) & 0x1)
		break;
	    DELAY(1);
	}
	if (j == 100) {
	    printf("PnP device failed to report resource data\n");
	    return 0;
	}
	outb(_PNP_ADDRESS, PNP_RESOURCE_DATA);
	buffer[i] = inb((rd_port << 2) | 0x3);
	DEB(printf("--- get_resource_info: got 0x%02x\n",(unsigned)buffer[i]));
    }
    return 1;
}

void
report_dma_info (x)
	int x;
{
    char *s1=NULL, *s2=NULL, *s3=NULL, *s4=NULL, *s5=NULL;

    switch (x & 0x3) {
    case 0:
	s1="8-bit";
	break;
    case 1:
	s1="8/16-bit";
	break;
    case 2:
	s1="16-bit";
	break;
#ifdef DIAGNOSTIC
    case 3:
	s1="Reserved";
	break;
#endif
    }

    s2 = (x & 0x4) ? "bus master" : "not a bus master";

    s3 = (x & 0x8) ? "count by byte" : "";

    s4 = (x & 0x10) ? "count by word" : "";

    switch ((x & 0x60) >> 5) {
    case 0:
	s5="Compatibility mode";
	break;
    case 1:
	s5="Type A";
	break;
    case 2:
	s5="Type B";
	break;
    case 3:
	s5="Type F";
	break;
    }
    printf("\t%s, %s, %s, %s, %s\n",s1,s2,s3,s4,s5);
}


void
report_memory_info (int x)
{
    if (x & 0x1)
	printf ("Memory Range: Writeable\n");
    else
	printf ("Memory Range: Not writeable (ROM)\n");

    if (x & 0x2)
	printf ("Memory Range: Read-cacheable, write-through\n");
    else
	printf ("Memory Range: Non-cacheable\n");

    if (x & 0x4)
	printf ("Memory Range: Decode supports high address\n");
    else
	printf ("Memory Range: Decode supports range length\n");

    switch ((x & 0x18) >> 3) {
    case 0:
	printf ("Memory Range: 8-bit memory only\n");
	break;
    case 1:
	printf ("Memory Range: 16-bit memory only\n");
	break;
    case 2:
	printf ("Memory Range: 8-bit and 16-bit memory supported\n");
	break;
#ifdef DIAGNOSTIC
    case 3:
	printf ("Memory Range: Reserved\n");
	break;
#endif
    }

    if (x & 0x20)
	printf ("Memory Range: Memory is shadowable\n");
    else
	printf ("Memory Range: Memory is not shadowable\n");

    if (x & 0x40)
	printf ("Memory Range: Memory is an expansion ROM\n");
    else
	printf ("Memory Range: Memory is not an expansion ROM\n");

#ifdef DIAGNOSTIC
    if (x & 0x80)
	printf ("Memory Range: Reserved (Device is brain-damaged)\n");
#endif
}


/*
 *  Small Resource Tag Handler
 *
 *  Returns 1 if checksum was valid (and an END_TAG was received).
 *  Returns -1 if checksum was invalid (and an END_TAG was received).
 *  Returns 0 for other tags.
 */
int
handle_small_res(u_char *resinfo, int item, int len)
{
    int i;

    DEB(printf("*** ITEM 0x%04x len %d detected\n", item, len));

    switch (item) {
    default:
	printf("*** ITEM 0x%02x detected\n", item);
	break;
    case PNP_TAG_VERSION:
	printf("PnP Version %d.%d, Vendor Version %d\n",
	    resinfo[0] >> 4, resinfo[0] & (0xf), resinfo[1]);
	break;
    case PNP_TAG_LOGICAL_DEVICE:
	printf("\nLogical Device ID: %c%c%c%02x%02x 0x%08x #%d\n",
		((resinfo[0] & 0x7c) >> 2) + 64,
		(((resinfo[0] & 0x03) << 3) |
		((resinfo[1] & 0xe0) >> 5)) + 64,
		(resinfo[1] & 0x1f) + 64,
		resinfo[2], resinfo[3], *(int *)(resinfo),
		logdevs++);

	if (resinfo[4] & 0x1)
	    printf ("\tDevice powers up active\n"); /* XXX */
	if (resinfo[4] & 0x2)
	    printf ("\tDevice supports I/O Range Check\n");
	if (resinfo[4] > 0x3)
	    printf ("\tReserved register funcs %02x\n",
		resinfo[4]);

	if (len == 6)
	    printf("\tVendor register funcs %02x\n", resinfo[5]);
	break;
    case PNP_TAG_COMPAT_DEVICE:
	printf("Compatible Device ID: %c%c%c%02x%02x (%08x)\n",
		((resinfo[0] & 0x7c) >> 2) + 64,
		(((resinfo[0] & 0x03) << 3) |
		((resinfo[1] & 0xe0) >> 5)) + 64,
		(resinfo[1] & 0x1f) + 64,
		resinfo[2], resinfo[3], *(int *)resinfo);
	break;
    case PNP_TAG_IRQ_FORMAT:
	printf("    IRQ: ");

	for (i = 0; i < 8; i++)
	    if (resinfo[0] & (1<<i))
		printf("%d ", i);
	for (i = 0; i < 8; i++)
	    if (resinfo[1] & (1<<i))
		printf("%d ", i + 8);
	if (len == 3) {
	    if (resinfo[2] & 0x1)
		printf("IRQ: High true edge sensitive\n");
	    if (resinfo[2] & 0x2)
		printf("IRQ: Low true edge sensitive\n");
	    if (resinfo[2] & 0x4)
		printf("IRQ: High true level sensitive\n");
	    if (resinfo[2] & 0x8)
		printf("IRQ: Low true level sensitive\n");
	} else {
	    printf(" - only one type (true/edge)\n");
	}
	break;
    case PNP_TAG_DMA_FORMAT:
	printf("    DMA: channel(s) ");
	for (i = 0; i < 8; i++)
	    if (resinfo[0] & (1<<i))
		printf("%d ", i);
	printf ("\n");
	report_dma_info (resinfo[1]);
	break;
    case PNP_TAG_START_DEPENDANT:
	printf("TAG Start DF\n");
	if (len == 1) {
	    switch (resinfo[0]) {
	    case 0:
		printf("Good Configuration\n");
		break;
	    case 1:
		printf("Acceptable Configuration\n");
		break;
	    case 2:
		printf("Sub-optimal Configuration\n");
		break;
	    }
	}
	break;
    case PNP_TAG_END_DEPENDANT:
	printf("TAG End DF\n");
	break;
    case PNP_TAG_IO_RANGE:
	printf("    I/O Range 0x%x .. 0x%x, alignment 0x%x, len 0x%x\n",
	    resinfo[1] + (resinfo[2] << 8),
	    resinfo[3] + (resinfo[4] << 8),
	    resinfo[5], resinfo[6] );
	if (resinfo[0])
	    printf("\t[16-bit addr]\n");
	else
	    printf("\t[not 16-bit addr]\n");
	break;
    case PNP_TAG_IO_FIXED:
	printf ("    FIXED I/O base address 0x%x length 0x%x\n",
	    resinfo[0] + ( (resinfo[1] & 3 ) << 8), /* XXX */
	    resinfo[2]);
	break;
#ifdef DIAGNOSTIC
    case PNP_TAG_RESERVED:
	printf("Reserved Tag Detected\n");
	break;
#endif
    case PNP_TAG_VENDOR:
	printf("*** Small Vendor Tag Detected\n");
	break;
    case PNP_TAG_END:
	printf("End Tag\n\n");
	/* XXX Record and Verify Checksum */
	return 1;
	break;
    }
    return 0;
}


void
handle_large_res(u_char *resinfo, int item, int len)
{
    int i;

    DEB(printf("*** Large ITEM %d len %d found\n", item, len));
    switch (item) {
    case PNP_TAG_MEMORY_RANGE:
	report_memory_info(resinfo[0]);
	printf("Memory range minimum address: 0x%x\n",
		(resinfo[1] << 8) + (resinfo[2] << 16));
	printf("Memory range maximum address: 0x%x\n",
		(resinfo[3] << 8) + (resinfo[4] << 16));
	printf("Memory range base alignment: 0x%x\n",
		(i = (resinfo[5] + (resinfo[6] << 8))) ? i : (1 << 16));
	printf("Memory range length: 0x%x\n",
		(resinfo[7] + (resinfo[8] << 8)) * 256);
	break;
    case PNP_TAG_ID_ANSI:
	printf("Device Description: ");

	for (i = 0; i < len; i++) {
	    if (resinfo[i]) /* XXX */
		printf("%c", resinfo[i]);
	}
	printf("\n");
	break;
    case PNP_TAG_ID_UNICODE:
	printf("ID String Unicode Detected (Undefined)\n");
	break;
    case PNP_TAG_LARGE_VENDOR:
	printf("Large Vendor Defined Detected\n");
	break;
    case PNP_TAG_MEMORY32_RANGE:
	printf("32bit Memory Range Desc Unimplemented\n");
	break;
    case PNP_TAG_MEMORY32_FIXED:
	printf("32bit Fixed Location Desc Unimplemented\n");
	break;
#ifdef DIAGNOSTIC
    case PNP_TAG_LARGE_RESERVED:
	printf("Large Reserved Tag Detected\n");
	break;
#endif
    }
}


/*
 * Dump all the information about configurations.
 */
void
dump_resdata(u_char *data, int csn)
{
    int i, large_len;

    u_char tag, *resinfo;

    DDB(printf("\nCard assigned CSN #%d\n", csn));
    printf("Vendor ID %c%c%c%02x%02x (0x%08x), Serial Number 0x%08x\n",
	    ((data[0] & 0x7c) >> 2) + 64,
	    (((data[0] & 0x03) << 3) | ((data[1] & 0xe0) >> 5)) + 64,
	    (data[1] & 0x1f) + 64, data[2], data[3],
	    *(int *)&(data[0]),
	    *(int *)&(data[4]));

    pnp_write(PNP_SET_CSN, csn); /* Move this out of this function XXX */
    outb(_PNP_ADDRESS, PNP_STATUS);

    /* Allows up to 1kb of Resource Info,  Should be plenty */
    for (i = 0; i < 1024; i++) {
	if (!get_resource_info(&tag, 1))
	    break;

	if (PNP_RES_TYPE(tag) == 0) {
	    /* Handle small resouce data types */

	    resinfo = malloc(PNP_SRES_LEN(tag));
	    if (!get_resource_info(resinfo, PNP_SRES_LEN(tag)))
		break;

	    if (handle_small_res(resinfo, PNP_SRES_NUM(tag), PNP_SRES_LEN(tag)) == 1)
		break;
	    free(resinfo);
	} else {
	    /* Handle large resouce data types */
	    u_char buf[2];
	    if (!get_resource_info((char *)buf, 2))
		break;
	    large_len = (buf[1] << 8) + buf[0];

	    resinfo = malloc(large_len);
	    if (!get_resource_info(resinfo, large_len))
		break;

	    handle_large_res(resinfo, PNP_LRES_NUM(tag), large_len);
	    free(resinfo);
	}
    }
    printf("Successfully got %d resources, %d logical fdevs\n", i,
	    logdevs);
    printf("-- card select # 0x%04x\n", pnp_read(PNP_SET_CSN));
    printf("\nCSN %c%c%c%02x%02x (0x%08x), Serial Number 0x%08x\n",
	    ((data[0] & 0x7c) >> 2) + 64,
	    (((data[0] & 0x03) << 3) | ((data[1] & 0xe0) >> 5)) + 64,
	    (data[1] & 0x1f) + 64, data[2], data[3],
	    *(int *)&(data[0]),
	    *(int *)&(data[4]));

    for (i=0; i<logdevs; i++) {
	int j;

	pnp_write(PNP_SET_LDN, i);

	printf("\nLogical device #%d\n", pnp_read(PNP_SET_LDN) );
	printf("IO: ");
	for (j=0; j<8; j++)
	    printf(" 0x%02x%02x", pnp_read(PNP_IO_BASE_HIGH(j)),
		pnp_read(PNP_IO_BASE_LOW(j)));
	printf("\nIRQ %d %d\n",
	    pnp_read(PNP_IRQ_LEVEL(0)), pnp_read(PNP_IRQ_LEVEL(1)) );
	printf("DMA %d %d\n",
	    pnp_read(PNP_DMA_CHANNEL(0)), pnp_read(PNP_DMA_CHANNEL(1)) );
	printf("IO range check 0x%02x activate 0x%02x\n",
	    pnp_read(PNP_IO_RANGE_CHECK), pnp_read(PNP_ACTIVATE) );
    }
}


/*
 * Run the isolation protocol. Use rd_port as the READ_DATA port
 * value (caller should try multiple READ_DATA locations before giving
 * up). Upon exiting, all cards are aware that they should use rd_port
 * as the READ_DATA port;
 *
 */
int
isolation_protocol()
{
    int csn;
    u_char data[9];

    send_Initiation_LFSR();

    /* Reset CSN for All Cards */
    pnp_write(PNP_CONFIG_CONTROL, 0x04);

    for (csn = 1; (csn < PNP_MAX_CARDS); csn++) {
	/* Wake up cards without a CSN */
	logdevs = 0 ;
	pnp_write(PNP_WAKE, 0);
	pnp_write(PNP_SET_RD_DATA, rd_port);
	outb(_PNP_ADDRESS, PNP_SERIAL_ISOLATION);
	DELAY(1000);	/* Delay 1 msec */

	if (get_serial(data))
	    dump_resdata(data, csn);
	else
	    break;
    }
    return csn - 1;
}


int
main(int argc, char **argv)
{
    int num_pnp_devs;

#ifdef __i386__
    /* Hey what about a i386_iopl() call :) */
    if (open("/dev/io", O_RDONLY) < 0)
	errx(1, "can't get I/O privilege");
#endif

    printf("Checking for Plug-n-Play devices...\n");

    /* Try various READ_DATA ports from 0x203-0x3ff */
    for (rd_port = 0x80; (rd_port < 0xff); rd_port += 0x10) {
	DEB(printf("Trying Read_Port at %x...\n", (rd_port << 2) | 0x3) );
	num_pnp_devs = isolation_protocol();
	if (num_pnp_devs)
	    break;
    }
    if (!num_pnp_devs) {
	printf("No Plug-n-Play devices were found\n");
	return (0);
    }
    return (0);
}
