/*
 * Linux defines for values that are not yet included in common C libraries
 * Copyright (c) 2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef LINUX_DEFINES_H
#define LINUX_DEFINES_H

#ifndef SO_WIFI_STATUS
# if defined(__sparc__)
#  define SO_WIFI_STATUS	0x0025
# elif defined(__parisc__)
#  define SO_WIFI_STATUS	0x4022
# else
#  define SO_WIFI_STATUS	41
# endif

# define SCM_WIFI_STATUS	SO_WIFI_STATUS
#endif

#ifndef SO_EE_ORIGIN_TXSTATUS
#define SO_EE_ORIGIN_TXSTATUS	4
#endif

#ifndef PACKET_TX_TIMESTAMP
#define PACKET_TX_TIMESTAMP	16
#endif

#ifndef IFF_LOWER_UP
#define IFF_LOWER_UP   0x10000         /* driver signals L1 up         */
#endif
#ifndef IFF_DORMANT
#define IFF_DORMANT    0x20000         /* driver signals dormant       */
#endif

#ifndef IF_OPER_DORMANT
#define IF_OPER_DORMANT 5
#endif
#ifndef IF_OPER_UP
#define IF_OPER_UP 6
#endif

#endif /* LINUX_DEFINES_H */
