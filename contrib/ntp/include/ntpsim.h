/* ntpsim.h
 *
 * The header file for the ntp discrete event simulator. 
 *
 * Written By:	Sachin Kamboj
 *		University of Delaware
 *		Newark, DE 19711
 * Copyright (c) 2006
 */

#ifndef NTPSIM_H
#define NTPSIM_H

#include <stdio.h>
#include <math.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <arpa/inet.h>
#include "ntp_syslog.h"
#include "ntp_fp.h"
#include "ntp.h"
#include "ntp_select.h"
#include "ntp_malloc.h"
#include "ntp_refclock.h"
#include "recvbuff.h"
#include "ntp_io.h"
#include "ntp_stdlib.h"
#include "ntp_prio_q.h"

/* CONSTANTS */

#ifdef PI
# undef PI
#endif
#define PI 3.1415926535         /* The world's most famous constant */
#define SIM_TIME 86400		/* end simulation time */
#define NET_DLY .001            /* network delay */
#define PROC_DLY .001		/* processing delay */
#define BEEP_DLY 3600           /* beep interval (s) */


/* Discrete Event Queue
 * --------------------
 * The NTP simulator is a discrete event simulator.
 *
 * Central to this simulator is an event queue which is a priority queue
 * in which the "priority" is given by the time of arrival of the event.
 *
 * A discrete set of events can happen and are stored in the queue to arrive
 * at a particular time.
 */

/* Possible Discrete Events */

typedef enum {
    BEEP,          /* Event to record simulator stats */
    CLOCK,         /* Event to advance the clock to the specified time */
    TIMER,         /* Event that designates a timer interrupt. */
    PACKET         /* Event that designates arrival of a packet */
} funcTkn;


/* Event information */

typedef struct {
    double time;       /* Time at which event occurred */
    funcTkn function;  /* Type of event that occured */
    union {
        struct pkt evnt_pkt;
        struct recvbuf evnt_buf;
    } buffer;          /* Other data associated with the event */
#define ntp_pkt buffer.evnt_pkt
#define rcv_buf buffer.evnt_buf
} Event;


/* Server Script Information */
typedef struct script_info_tag script_info;
struct script_info_tag {
	script_info *	link;
	double		duration;
	double		freq_offset;
	double		wander;
	double		jitter; 
	double		prop_delay;
	double		proc_delay;
};

typedef DECL_FIFO_ANCHOR(script_info) script_info_fifo;


/* Server Structures */

typedef struct server_info_tag server_info;
struct server_info_tag {
	server_info *		link;
	double			server_time;
	sockaddr_u *		addr;
	script_info_fifo *	script;
	script_info *		curr_script;
};

typedef DECL_FIFO_ANCHOR(server_info) server_info_fifo;


/* Simulation control information */

typedef struct Sim_Info {
    double sim_time;      /* Time in the simulation */
    double end_time;      /* Time at which simulation needs to be ended */
    double beep_delay;    /* Delay between simulation "beeps" at which
                             simulation  stats are recorded. */
    int num_of_servers;   /* Number of servers in the simulation */
    server_info *servers; /* Pointer to array of servers */
} sim_info;


/* Local Clock (Client) Variables */

typedef struct Local_Clock_Info {
    double local_time;		/* Client disciplined time */
    double adj;			/* Remaining time correction */
    double slew;		/* Correction Slew Rate */
    double last_read_time;	/* Last time the clock was read */
} local_clock_info;

extern local_clock_info simclock; /* Local Clock Variables */
extern sim_info simulation;	  /* Simulation Control Variables */

/* Function Prototypes */

int	 ntpsim			(int argc, char *argv[]);
Event    *event			(double t, funcTkn f);
void     sim_event_timer	(Event *e);
int      simulate_server	(sockaddr_u *serv_addr, endpt *inter,
				 struct pkt *rpkt);
void     sim_update_clocks	(Event *e);
void     sim_event_recv_packet	(Event *e);
void     sim_event_beep		(Event *e);
void     abortsim		(char *errmsg);
double	 gauss			(double, double);
double	 poisson		(double, double);
void     create_server_associations(void);

#endif	/* NTPSIM_H */
