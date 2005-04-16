/*
 * Smp timebase synchronization for ppc.
 *
 * Copyright (C) 2003 Samuel Rydh (samuel@ibrium.se)
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/unistd.h>
#include <linux/init.h>
#include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/time.h>

#define NUM_ITER		300

enum {
	kExit=0, kSetAndTest, kTest
};

static struct {
	volatile int		tbu;
	volatile int		tbl;
	volatile int		mark;
	volatile int		cmd;
	volatile int		handshake;
	int			filler[3];

	volatile int		ack;
	int			filler2[7];

	volatile int		race_result;
} *tbsync;

static volatile int		running;

static void __devinit
enter_contest( int mark, int add )
{
	while( (int)(get_tbl() - mark) < 0 )
		tbsync->race_result = add;
}

void __devinit
smp_generic_take_timebase( void )
{
	int cmd, tbl, tbu;

	local_irq_disable();
	while( !running )
		;
	rmb();

	for( ;; ) {
		tbsync->ack = 1;
		while( !tbsync->handshake )
			;
		rmb();

		cmd = tbsync->cmd;
		tbl = tbsync->tbl;
		tbu = tbsync->tbu;
		tbsync->ack = 0;
		if( cmd == kExit )
			return;

		if( cmd == kSetAndTest ) {
			while( tbsync->handshake )
				;
			asm volatile ("mttbl %0" :: "r" (tbl) );
			asm volatile ("mttbu %0" :: "r" (tbu) );
		} else {
			while( tbsync->handshake )
				;
		}
		enter_contest( tbsync->mark, -1 );
	}
	local_irq_enable();
}

static int __devinit
start_contest( int cmd, int offset, int num )
{
	int i, tbu, tbl, mark, score=0;

	tbsync->cmd = cmd;

	local_irq_disable();
	for( i=-3; i<num; ) {
		tbl = get_tbl() + 400;
		tbsync->tbu = tbu = get_tbu();
		tbsync->tbl = tbl + offset;
		tbsync->mark = mark = tbl + 400;

		wmb();

		tbsync->handshake = 1;
		while( tbsync->ack )
			;

		while( (int)(get_tbl() - tbl) <= 0 )
			;
		tbsync->handshake = 0;
		enter_contest( mark, 1 );

		while( !tbsync->ack )
			;

		if( tbsync->tbu != get_tbu() || ((tbsync->tbl ^ get_tbl()) & 0x80000000) )
			continue;
		if( i++ > 0 )
			score += tbsync->race_result;
	}
	local_irq_enable();
	return score;
}

void __devinit
smp_generic_give_timebase( void )
{
	int i, score, score2, old, min=0, max=5000, offset=1000;

	printk("Synchronizing timebase\n");

	/* if this fails then this kernel won't work anyway... */
	tbsync = kmalloc( sizeof(*tbsync), GFP_KERNEL );
	memset( tbsync, 0, sizeof(*tbsync) );
	mb();
	running = 1;

	while( !tbsync->ack )
		;

	/* binary search */
	for( old=-1 ; old != offset ; offset=(min+max)/2 ) {
		score = start_contest( kSetAndTest, offset, NUM_ITER );

		printk("score %d, offset %d\n", score, offset );

		if( score > 0 )
			max = offset;
		else
			min = offset;
		old = offset;
	}
	score = start_contest( kSetAndTest, min, NUM_ITER );
	score2 = start_contest( kSetAndTest, max, NUM_ITER );

	printk( "Min %d (score %d), Max %d (score %d)\n", min, score, max, score2 );
	score = abs( score );
	score2 = abs( score2 );
	offset = (score < score2) ? min : max;

	/* guard against inaccurate mttb */
	for( i=0; i<10; i++ ) {
		start_contest( kSetAndTest, offset, NUM_ITER/10 );

		if( (score2=start_contest(kTest, offset, NUM_ITER)) < 0 )
			score2 = -score2;
		if( score2 <= score || score2 < 20 )
			break;
	}
	printk("Final offset: %d (%d/%d)\n", offset, score2, NUM_ITER );

	/* exiting */
	tbsync->cmd = kExit;
	wmb();
	tbsync->handshake = 1;
	while( tbsync->ack )
		;
	tbsync->handshake = 0;
	kfree( tbsync );
	tbsync = NULL;
	running = 0;

	/* all done */
	smp_tb_synchronized = 1;
}
