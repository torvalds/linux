/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : apdbg.c                                               */
/*                                                                      */
/*  Abstract                                                            */
/*      Debug tools                                                     */
/*                                                                      */
/*  NOTES                                                               */
/*      None                                                            */
/*                                                                      */
/************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/sockios.h>

#define ZM_IOCTL_REG_READ           0x01
#define ZM_IOCTL_REG_WRITE          0x02
#define ZM_IOCTL_MEM_DUMP           0x03
#define ZM_IOCTL_REG_DUMP           0x05
#define ZM_IOCTL_TXD_DUMP           0x06
#define ZM_IOCTL_RXD_DUMP           0x07
#define ZM_IOCTL_MEM_READ           0x0B
#define ZM_IOCTL_MEM_WRITE          0x0C
#define ZM_IOCTL_DMA_TEST           0x10
#define ZM_IOCTL_REG_TEST           0x11
#define ZM_IOCTL_TEST               0x80
#define ZM_IOCTL_TALLY              0x81 //CWYang(+)
#define ZM_IOCTL_RTS                0xA0
#define ZM_IOCTL_MIX_MODE           0xA1
#define ZM_IOCTL_FRAG               0xA2
#define ZM_IOCTL_SCAN               0xA3
#define ZM_IOCTL_KEY                0xA4
#define ZM_IOCTL_RATE               0xA5
#define ZM_IOCTL_ENCRYPTION_MODE    0xA6
#define ZM_IOCTL_GET_TXCNT          0xA7
#define ZM_IOCTL_GET_DEAGG_CNT      0xA8
#define ZM_IOCTL_DURATION_MODE      0xA9
#define ZM_IOCTL_SET_AES_KEY        0xAA
#define ZM_IOCTL_SET_AES_MODE       0xAB
#define ZM_IOCTL_SIGNAL_STRENGTH    0xAC //CWYang(+)
#define ZM_IOCTL_SIGNAL_QUALITY     0xAD //CWYang(+)
#define ZM_IOCTL_SET_PIBSS_MODE     0xAE
#define	ZDAPIOCTL                   SIOCDEVPRIVATE

struct zdap_ioctl {
	unsigned short cmd;                /* Command to run */
	unsigned int   addr;                /* Length of the data buffer */
	unsigned int   value;               /* Pointer to the data buffer */
	unsigned char data[0x100];
};

/* Declaration of macro and function for handling WEP Keys */

#if 0

#define SKIP_ELEM { \
    while(isxdigit(*p)) \
        p++; \
}

#define SKIP_DELIMETER { \
    if(*p == ':' || *p == ' ') \
        p++; \
}

#endif

char hex(char);
unsigned char asctohex(char *str);

char *prgname;

int set_ioctl(int sock, struct ifreq *req)
{
    if (ioctl(sock, ZDAPIOCTL, req) < 0) {
        fprintf(stderr, "%s: ioctl(SIOCGIFMAP): %s\n",
                prgname, strerror(errno));
        return -1;
    }

    return 0;
}


int read_reg(int sock, struct ifreq *req)
{
    struct zdap_ioctl *zdreq = 0;

    if (!set_ioctl(sock, req))
        return -1;

    //zdreq = (struct zdap_ioctl *)req->ifr_data;
    //printf( "reg = %4x, value = %4x\n", zdreq->addr, zdreq->value);

    return 0;
}


int read_mem(int sock, struct ifreq *req)
{
    struct zdap_ioctl *zdreq = 0;
    int i;

    if (!set_ioctl(sock, req))
        return -1;

    /*zdreq = (struct zdap_ioctl *)req->ifr_data;
    printf( "dump mem from %x, length = %x\n", zdreq->addr, zdreq->value);

    for (i=0; i<zdreq->value; i++) {
        printf("%02x", zdreq->data[i]);
        printf(" ");

        if ((i>0) && ((i+1)%16 == 0))
            printf("\n");
    }*/

    return 0;
}


int main(int argc, char **argv)
{
    int sock;
    int addr, value;
    struct ifreq req;
    char *action = NULL;
    struct zdap_ioctl zdreq;

    prgname = argv[0];

    if (argc < 3) {
        fprintf(stderr,"%s: usage is \"%s <ifname> <operation> [<address>] [<value>]\"\n",
                prgname, prgname);
        fprintf(stderr,"valid operation: read, write, mem, reg,\n");
        fprintf(stderr,"               : txd, rxd, rmem, wmem\n");
        fprintf(stderr,"               : dmat, regt, test\n");

        fprintf(stderr,"    scan, Channel Scan\n");
        fprintf(stderr,"    rts <decimal>, Set RTS Threshold\n");
        fprintf(stderr,"    frag <decimal>, Set Fragment Threshold\n");
        fprintf(stderr,"    rate <0-28>, 0:AUTO, 1-4:CCK, 5-12:OFDM, 13-28:HT\n");
        fprintf(stderr,"    TBD mix <0 or 1>, Set 1 to enable mixed mode\n");
        fprintf(stderr,"    enc, <0-3>, 0=>OPEN, 1=>WEP64, 2=>WEP128, 3=>WEP256\n");
        fprintf(stderr,"    skey <key>, Set WEP key\n");
        fprintf(stderr,"    txcnt, Get TxQ Cnt\n");
        fprintf(stderr,"    dagcnt, Get Deaggregate Cnt\n");
        fprintf(stderr,"    durmode <mode>, Set Duration Mode 0=>HW, 1=>SW\n");
        fprintf(stderr,"    aeskey <user> <key>\n");
        fprintf(stderr,"    aesmode <mode>\n");
        fprintf(stderr,"    wlanmode <0,1> 0:Station mode, 1:PIBSS mode\n");
        fprintf(stderr,"    tal <0,1>, Get Current Tally Info, 0=>read, 1=>read and reset\n");

        exit(1);
    }

    strcpy(req.ifr_name, argv[1]);
    zdreq.addr = 0;
    zdreq.value = 0;

    /* a silly raw socket just for ioctl()ling it */
    sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0) {
        fprintf(stderr, "%s: socket(): %s\n", argv[0], strerror(errno));
        exit(1);
    }

    if (argc >= 4)
    {
        sscanf(argv[3], "%x", &addr);
    }

    if (argc >= 5)
    {
        sscanf(argv[4], "%x", &value);
    }

    zdreq.addr = addr;
    zdreq.value = value;

    if (!strcmp(argv[2], "read"))
    {
        zdreq.cmd = ZM_IOCTL_REG_READ;
    }
    else if (!strcmp(argv[2], "mem"))
    {
        zdreq.cmd = ZM_IOCTL_MEM_DUMP;
    }
    else if (!strcmp(argv[2], "write"))
    {
        zdreq.cmd = ZM_IOCTL_REG_WRITE;
    }
    else if (!strcmp(argv[2], "reg"))
    {
        zdreq.cmd = ZM_IOCTL_REG_DUMP;
    }
    else if (!strcmp(argv[2], "txd"))
    {
        zdreq.cmd = ZM_IOCTL_TXD_DUMP;
    }
    else if (!strcmp(argv[2], "rxd"))
    {
        zdreq.cmd = ZM_IOCTL_RXD_DUMP;
    }
    else if (!strcmp(argv[2], "rmem"))
    {
        zdreq.cmd = ZM_IOCTL_MEM_READ;
    }
    else if (!strcmp(argv[2], "wmem"))
    {
        zdreq.cmd = ZM_IOCTL_MEM_WRITE;
    }
    else if (!strcmp(argv[2], "dmat"))
    {
        zdreq.cmd = ZM_IOCTL_DMA_TEST;
    }
    else if (!strcmp(argv[2], "regt"))
    {
        zdreq.cmd = ZM_IOCTL_REG_TEST;
    }
    else if (!strcmp(argv[2], "test"))
    {
        zdreq.cmd = ZM_IOCTL_TEST;
    }
    else if (!strcmp(argv[2], "tal"))
    {
        sscanf(argv[3], "%d", &addr);
        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_TALLY;
    }
    else if (!strcmp(argv[2], "rts"))
    {
        sscanf(argv[3], "%d", &addr);
        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_RTS;
    }
    else if (!strcmp(argv[2], "mix"))
    {
        zdreq.cmd = ZM_IOCTL_MIX_MODE;
    }
    else if (!strcmp(argv[2], "frag"))
    {
        sscanf(argv[3], "%d", &addr);
        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_FRAG;
    }
    else if (!strcmp(argv[2], "scan"))
    {
        zdreq.cmd = ZM_IOCTL_SCAN;
    }
    else if (!strcmp(argv[2], "skey"))
    {
        zdreq.cmd = ZM_IOCTL_KEY;

        if (argc >= 4)
        {
            unsigned char temp[29];
            int i;
            int keyLen;
            int encType;

            keyLen = strlen(argv[3]);

            if (keyLen == 10)
            {
                sscanf(argv[3], "%02x%02x%02x%02x%02x", &temp[0], &temp[1],
                        &temp[2], &temp[3], &temp[4]);
            }
            else if (keyLen == 26)
            {
                sscanf(argv[3], "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                        &temp[0], &temp[1], &temp[2], &temp[3], &temp[4],
                        &temp[5], &temp[6], &temp[7], &temp[8], &temp[9],
                         &temp[10], &temp[11], &temp[12]);
            }
            else if (keyLen == 58)
            {
                sscanf(argv[3], "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
                        &temp[0], &temp[1], &temp[2], &temp[3], &temp[4],
                        &temp[5], &temp[6], &temp[7], &temp[8], &temp[9],
                        &temp[10], &temp[11], &temp[12], &temp[13], &temp[14],
                        &temp[15], &temp[16], &temp[17], &temp[18], &temp[19],
                        &temp[20], &temp[21], &temp[22], &temp[23], &temp[24],
                        &temp[25], &temp[26], &temp[27], &temp[28]);
            }
            else
            {
                fprintf(stderr, "Invalid key length\n");
                exit(1);
            }
            zdreq.addr = keyLen/2;

            for(i=0; i<zdreq.addr; i++)
            {
                zdreq.data[i] = temp[i];
            }
        }
        else
        {
            printf("Error : Key required!\n");
        }
    }
    else if (!strcmp(argv[2], "rate"))
    {
        sscanf(argv[3], "%d", &addr);

        if (addr > 28)
        {
            fprintf(stderr, "Invalid rate, range:0~28\n");
            exit(1);
        }
        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_RATE;
    }
    else if (!strcmp(argv[2], "enc"))
    {
        sscanf(argv[3], "%d", &addr);

        if (addr > 3)
        {
            fprintf(stderr, "Invalid encryption mode, range:0~3\n");
            exit(1);
        }

        if (addr == 2)
        {
            addr = 5;
        }
        else if (addr == 3)
        {
            addr = 6;
        }

        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_ENCRYPTION_MODE;
    }
    else if (!strcmp(argv[2], "txcnt"))
    {
        zdreq.cmd = ZM_IOCTL_GET_TXCNT;
    }
    else if (!strcmp(argv[2], "dagcnt"))
    {
        sscanf(argv[3], "%d", &addr);

        if (addr != 0 && addr != 1)
        {
            fprintf(stderr, "The value should be 0 or 1\n");
            exit(0);
        }

        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_GET_DEAGG_CNT;
    }
    else if (!strcmp(argv[2], "durmode"))
    {
        sscanf(argv[3], "%d", &addr);

        if (addr != 0 && addr != 1)
        {
            fprintf(stderr, "The Duration mode should be 0 or 1\n");
            exit(0);
        }

        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_DURATION_MODE;
    }
    else if (!strcmp(argv[2], "aeskey"))
    {
        unsigned char temp[16];
        int i;

        sscanf(argv[3], "%d", &addr);

        sscanf(argv[4], "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", &temp[0], &temp[1], &temp[2], &temp[3], &temp[4], &temp[5], &temp[6], &temp[7], &temp[8], &temp[9], &temp[10], &temp[11], &temp[12], &temp[13], &temp[14], &temp[15]);

        for(i = 0; i < 16; i++)
        {
            zdreq.data[i] = temp[i];
        }

        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_SET_AES_KEY;
    }
    else if (!strcmp(argv[2], "aesmode"))
    {
        sscanf(argv[3], "%d", &addr);

        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_SET_AES_MODE;
    }
    else if (!strcmp(argv[2], "wlanmode"))
    {
        sscanf(argv[3], "%d", &addr);

        zdreq.addr = addr;
        zdreq.cmd = ZM_IOCTL_SET_PIBSS_MODE;
    }
    else
    {
	    fprintf(stderr, "error action\n");
        exit(1);
    }

    req.ifr_data = (char *)&zdreq;
    set_ioctl(sock, &req);

fail:
    exit(0);
}

unsigned char asctohex(char *str)
{
    unsigned char value;

    value = hex(*str) & 0x0f;
    value = value << 4;
    str++;
    value |= hex(*str) & 0x0f;

    return value;
}

char hex(char v)
{
    if(isdigit(v))
        return v - '0';
    else if(isxdigit(v))
        return (tolower(v) - 'a' + 10);
    else
        return 0;
}

