/* ntp_prio_q.h
 *
 * This file contains the structures and function prototypes for the
 * priority queue implementation used by the discrete event simulator.
 *
 * Written By:	Sachin Kamboj
 *		University of Delaware
 *		Newark, DE 19711
 * Copyright (c) 2006
 */

#ifndef NTP_PRIO_Q_H
#define NTP_PRIO_Q_H

#include <stddef.h>		/* size_t */

/* Structures for storing a priority queue 
 * ---------------------------------------
 */

typedef struct node {
	union {
		struct node *next;
		double d;
	} nodeu;
} node;
#define node_next nodeu.next

typedef int (*q_order_func)(const void *, const void *);

typedef struct Queue {
	q_order_func	get_order;
	node *		front;
	int		no_of_elements;
} queue;


/* FUNCTION PROTOTYPES
 * -------------------
 */
/* Define a function to create a FIFO queue */
#define create_queue()	create_priority_queue(&get_fifo_order)

void destroy_queue(queue *my_queue);
void free_node(void *my_node);
void *next_node(void *my_node);
int empty(queue *my_queue);
void *queue_head(queue *my_queue);
queue *enqueue(queue *my_queue, void *my_node);
void append_queue(queue *q1, queue *q2);
void *dequeue(queue *my_queue);
int get_no_of_elements(queue *my_queue);
int get_fifo_order(const void *el1, const void *el2);

/*
 * Preserve original callsite __FILE__ and __LINE__ for these 
 * malloc-like funcs when using MS C runtime debug heap.
 */
#ifdef _CRTDBG_MAP_ALLOC
# define create_priority_queue(order)	debug_create_priority_queue(order, __FILE__, __LINE__)
# define get_node(size)			debug_get_node(size, __FILE__, __LINE__)
#else
# define create_priority_queue(order)	debug_create_priority_queue(order)
# define get_node(size)			debug_get_node(size)
#endif

queue *debug_create_priority_queue(
	q_order_func	get_order
#ifdef _CRTDBG_MAP_ALLOC
	, const char *	sourcefile
	, int		line_num
#endif	
	);

void *debug_get_node(
	size_t		size
#ifdef _CRTDBG_MAP_ALLOC
	, const char *	sourcefile
	, int		line_num
#endif
	);

#endif	/* NTP_PRIO_Q_H */
