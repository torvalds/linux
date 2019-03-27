#ifndef UTILITIES_H
#define UTILITIES_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ntp.h"
#include "ntp_stdlib.h"
#include "lib_strbuf.h"

#define HLINE "--------------------------------------------------------------------------------\n"
#define PHLINE fprintf(output, HLINE);
#define STDLINE printf(HLINE);


void pkt_output(struct pkt *dpkg, int pkt_length, FILE *output);
void l_fp_output(l_fp *ts, FILE *output);
void l_fp_output_bin(l_fp *ts, FILE *output);
void l_fp_output_dec(l_fp *ts, FILE *output);

char *addrinfo_to_str(const struct addrinfo *addr);
char *ss_to_str(sockaddr_u *saddr);
char *tv_to_str(const struct timeval *tv);
const char * hostnameaddr(const char *, const sockaddr_u *);

#endif	/* UTILITIES_H */
