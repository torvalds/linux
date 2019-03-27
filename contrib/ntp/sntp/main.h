#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <isc/result.h>
#include <isc/net.h>

#include <ntp.h>
#include <ntp_unixtime.h>
#include <ntp_stdlib.h>
#include <ntp_intres.h>
#include <ntp_debug.h>
#include <timevalops.h>

#include <sntp-opts.h>

#include "crypto.h"

void	set_li_vn_mode(struct pkt *spkt, char leap, char version, char mode); 
extern int sntp_main(int argc, char **argv, const char *);
int	generate_pkt(struct pkt *x_pkt, const struct timeval *tv_xmt,
				  int key_id, struct key *pkt_key);
int	handle_pkt(int rpktl, struct pkt *rpkt, sockaddr_u *host,
		   const char *hostname);
void	offset_calculation(struct pkt *rpkt, int rpktl,
			   struct timeval *tv_dst, double *offset,
			   double *precision, double *root_dispersion);
int	on_wire(struct addrinfo *host, struct addrinfo *bcastaddr);
int	set_time(double offset);

#endif /* MAIN_H */
