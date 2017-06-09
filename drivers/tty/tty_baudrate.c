/*
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/termios.h>
#include <linux/tty.h>
#include <linux/export.h>


/*
 * Routine which returns the baud rate of the tty
 *
 * Note that the baud_table needs to be kept in sync with the
 * include/asm/termbits.h file.
 */
static const speed_t baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800,
#ifdef __sparc__
	76800, 153600, 307200, 614400, 921600
#else
	500000, 576000, 921600, 1000000, 1152000, 1500000, 2000000,
	2500000, 3000000, 3500000, 4000000
#endif
};

#ifndef __sparc__
static const tcflag_t baud_bits[] = {
	B0, B50, B75, B110, B134, B150, B200, B300, B600,
	B1200, B1800, B2400, B4800, B9600, B19200, B38400,
	B57600, B115200, B230400, B460800, B500000, B576000,
	B921600, B1000000, B1152000, B1500000, B2000000, B2500000,
	B3000000, B3500000, B4000000
};
#else
static const tcflag_t baud_bits[] = {
	B0, B50, B75, B110, B134, B150, B200, B300, B600,
	B1200, B1800, B2400, B4800, B9600, B19200, B38400,
	B57600, B115200, B230400, B460800, B76800, B153600,
	B307200, B614400, B921600
};
#endif

static int n_baud_table = ARRAY_SIZE(baud_table);

/**
 *	tty_termios_baud_rate
 *	@termios: termios structure
 *
 *	Convert termios baud rate data into a speed. This should be called
 *	with the termios lock held if this termios is a terminal termios
 *	structure. May change the termios data. Device drivers can call this
 *	function but should use ->c_[io]speed directly as they are updated.
 *
 *	Locking: none
 */

speed_t tty_termios_baud_rate(struct ktermios *termios)
{
	unsigned int cbaud;

	cbaud = termios->c_cflag & CBAUD;

#ifdef BOTHER
	/* Magic token for arbitrary speed via c_ispeed/c_ospeed */
	if (cbaud == BOTHER)
		return termios->c_ospeed;
#endif
	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~CBAUDEX;
		else
			cbaud += 15;
	}
	return baud_table[cbaud];
}
EXPORT_SYMBOL(tty_termios_baud_rate);

/**
 *	tty_termios_input_baud_rate
 *	@termios: termios structure
 *
 *	Convert termios baud rate data into a speed. This should be called
 *	with the termios lock held if this termios is a terminal termios
 *	structure. May change the termios data. Device drivers can call this
 *	function but should use ->c_[io]speed directly as they are updated.
 *
 *	Locking: none
 */

speed_t tty_termios_input_baud_rate(struct ktermios *termios)
{
#ifdef IBSHIFT
	unsigned int cbaud = (termios->c_cflag >> IBSHIFT) & CBAUD;

	if (cbaud == B0)
		return tty_termios_baud_rate(termios);

	/* Magic token for arbitrary speed via c_ispeed*/
	if (cbaud == BOTHER)
		return termios->c_ispeed;

	if (cbaud & CBAUDEX) {
		cbaud &= ~CBAUDEX;

		if (cbaud < 1 || cbaud + 15 > n_baud_table)
			termios->c_cflag &= ~(CBAUDEX << IBSHIFT);
		else
			cbaud += 15;
	}
	return baud_table[cbaud];
#else
	return tty_termios_baud_rate(termios);
#endif
}
EXPORT_SYMBOL(tty_termios_input_baud_rate);

/**
 *	tty_termios_encode_baud_rate
 *	@termios: ktermios structure holding user requested state
 *	@ispeed: input speed
 *	@ospeed: output speed
 *
 *	Encode the speeds set into the passed termios structure. This is
 *	used as a library helper for drivers so that they can report back
 *	the actual speed selected when it differs from the speed requested
 *
 *	For maximal back compatibility with legacy SYS5/POSIX *nix behaviour
 *	we need to carefully set the bits when the user does not get the
 *	desired speed. We allow small margins and preserve as much of possible
 *	of the input intent to keep compatibility.
 *
 *	Locking: Caller should hold termios lock. This is already held
 *	when calling this function from the driver termios handler.
 *
 *	The ifdefs deal with platforms whose owners have yet to update them
 *	and will all go away once this is done.
 */

void tty_termios_encode_baud_rate(struct ktermios *termios,
				  speed_t ibaud, speed_t obaud)
{
	int i = 0;
	int ifound = -1, ofound = -1;
	int iclose = ibaud/50, oclose = obaud/50;
	int ibinput = 0;

	if (obaud == 0)			/* CD dropped 		  */
		ibaud = 0;		/* Clear ibaud to be sure */

	termios->c_ispeed = ibaud;
	termios->c_ospeed = obaud;

#ifdef BOTHER
	/* If the user asked for a precise weird speed give a precise weird
	   answer. If they asked for a Bfoo speed they may have problems
	   digesting non-exact replies so fuzz a bit */

	if ((termios->c_cflag & CBAUD) == BOTHER)
		oclose = 0;
	if (((termios->c_cflag >> IBSHIFT) & CBAUD) == BOTHER)
		iclose = 0;
	if ((termios->c_cflag >> IBSHIFT) & CBAUD)
		ibinput = 1;	/* An input speed was specified */
#endif
	termios->c_cflag &= ~CBAUD;

	/*
	 *	Our goal is to find a close match to the standard baud rate
	 *	returned. Walk the baud rate table and if we get a very close
	 *	match then report back the speed as a POSIX Bxxxx value by
	 *	preference
	 */

	do {
		if (obaud - oclose <= baud_table[i] &&
		    obaud + oclose >= baud_table[i]) {
			termios->c_cflag |= baud_bits[i];
			ofound = i;
		}
		if (ibaud - iclose <= baud_table[i] &&
		    ibaud + iclose >= baud_table[i]) {
			/* For the case input == output don't set IBAUD bits
			   if the user didn't do so */
			if (ofound == i && !ibinput)
				ifound  = i;
#ifdef IBSHIFT
			else {
				ifound = i;
				termios->c_cflag |= (baud_bits[i] << IBSHIFT);
			}
#endif
		}
	} while (++i < n_baud_table);

	/*
	 *	If we found no match then use BOTHER if provided or warn
	 *	the user their platform maintainer needs to wake up if not.
	 */
#ifdef BOTHER
	if (ofound == -1)
		termios->c_cflag |= BOTHER;
	/* Set exact input bits only if the input and output differ or the
	   user already did */
	if (ifound == -1 && (ibaud != obaud || ibinput))
		termios->c_cflag |= (BOTHER << IBSHIFT);
#else
	if (ifound == -1 || ofound == -1)
		pr_warn_once("tty: Unable to return correct speed data as your architecture needs updating.\n");
#endif
}
EXPORT_SYMBOL_GPL(tty_termios_encode_baud_rate);

/**
 *	tty_encode_baud_rate		-	set baud rate of the tty
 *	@ibaud: input baud rate
 *	@obad: output baud rate
 *
 *	Update the current termios data for the tty with the new speed
 *	settings. The caller must hold the termios_rwsem for the tty in
 *	question.
 */

void tty_encode_baud_rate(struct tty_struct *tty, speed_t ibaud, speed_t obaud)
{
	tty_termios_encode_baud_rate(&tty->termios, ibaud, obaud);
}
EXPORT_SYMBOL_GPL(tty_encode_baud_rate);
