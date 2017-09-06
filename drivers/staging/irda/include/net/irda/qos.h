/*********************************************************************
 *                
 * Filename:      qos.h
 * Version:       1.0
 * Description:   Quality of Service definitions
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri Sep 19 23:21:09 1997
 * Modified at:   Thu Dec  2 13:51:54 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *     
 ********************************************************************/

#ifndef IRDA_QOS_H
#define IRDA_QOS_H

#include <linux/skbuff.h>

#include <net/irda/parameters.h>

#define PI_BAUD_RATE     0x01
#define PI_MAX_TURN_TIME 0x82
#define PI_DATA_SIZE     0x83
#define PI_WINDOW_SIZE   0x84
#define PI_ADD_BOFS      0x85
#define PI_MIN_TURN_TIME 0x86
#define PI_LINK_DISC     0x08

#define IR_115200_MAX 0x3f

/* Baud rates (first byte) */
#define IR_2400     0x01
#define IR_9600     0x02
#define IR_19200    0x04
#define IR_38400    0x08
#define IR_57600    0x10
#define IR_115200   0x20
#define IR_576000   0x40
#define IR_1152000  0x80

/* Baud rates (second byte) */
#define IR_4000000  0x01
#define IR_16000000 0x02

/* Quality of Service information */
typedef struct {
	__u32 value;
	__u16 bits; /* LSB is first byte, MSB is second byte */
} qos_value_t;

struct qos_info {
	magic_t magic;

	qos_value_t baud_rate;       /* IR_11520O | ... */
	qos_value_t max_turn_time;
	qos_value_t data_size;
	qos_value_t window_size;
	qos_value_t additional_bofs;
	qos_value_t min_turn_time;
	qos_value_t link_disc_time;
	
	qos_value_t power;
};

extern int sysctl_max_baud_rate;
extern int sysctl_max_inactive_time;

void irda_init_max_qos_capabilies(struct qos_info *qos);
void irda_qos_compute_intersection(struct qos_info *, struct qos_info *);

__u32 irlap_max_line_capacity(__u32 speed, __u32 max_turn_time);

void irda_qos_bits_to_value(struct qos_info *qos);

/* So simple, how could we not inline those two ?
 * Note : one byte is 10 bits if you include start and stop bits
 * Jean II */
#define irlap_min_turn_time_in_bytes(speed, min_turn_time) (	\
	speed * min_turn_time / 10000000			\
)
#define irlap_xbofs_in_usec(speed, xbofs) (			\
	xbofs * 10000000 / speed				\
)

#endif

