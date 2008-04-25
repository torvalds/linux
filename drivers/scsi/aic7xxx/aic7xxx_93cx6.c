/*
 * Interface for the 93C66/56/46/26/06 serial eeprom parts.
 *
 * Copyright (c) 1995, 1996 Daniel M. Eischen
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/aic7xxx/aic7xxx/aic7xxx_93cx6.c#19 $
 */

/*
 *   The instruction set of the 93C66/56/46/26/06 chips are as follows:
 *
 *               Start  OP	    *
 *     Function   Bit  Code  Address**  Data     Description
 *     -------------------------------------------------------------------
 *     READ        1    10   A5 - A0             Reads data stored in memory,
 *                                               starting at specified address
 *     EWEN        1    00   11XXXX              Write enable must precede
 *                                               all programming modes
 *     ERASE       1    11   A5 - A0             Erase register A5A4A3A2A1A0
 *     WRITE       1    01   A5 - A0   D15 - D0  Writes register
 *     ERAL        1    00   10XXXX              Erase all registers
 *     WRAL        1    00   01XXXX    D15 - D0  Writes to all registers
 *     EWDS        1    00   00XXXX              Disables all programming
 *                                               instructions
 *     *Note: A value of X for address is a don't care condition.
 *    **Note: There are 8 address bits for the 93C56/66 chips unlike
 *	      the 93C46/26/06 chips which have 6 address bits.
 *
 *   The 93C46 has a four wire interface: clock, chip select, data in, and
 *   data out.  In order to perform one of the above functions, you need
 *   to enable the chip select for a clock period (typically a minimum of
 *   1 usec, with the clock high and low a minimum of 750 and 250 nsec
 *   respectively).  While the chip select remains high, you can clock in
 *   the instructions (above) starting with the start bit, followed by the
 *   OP code, Address, and Data (if needed).  For the READ instruction, the
 *   requested 16-bit register contents is read from the data out line but
 *   is preceded by an initial zero (leading 0, followed by 16-bits, MSB
 *   first).  The clock cycling from low to high initiates the next data
 *   bit to be sent from the chip.
 */

#ifdef __linux__
#include "aic7xxx_osm.h"
#include "aic7xxx_inline.h"
#include "aic7xxx_93cx6.h"
#else
#include <dev/aic7xxx/aic7xxx_osm.h>
#include <dev/aic7xxx/aic7xxx_inline.h>
#include <dev/aic7xxx/aic7xxx_93cx6.h>
#endif

/*
 * Right now, we only have to read the SEEPROM.  But we make it easier to
 * add other 93Cx6 functions.
 */
struct seeprom_cmd {
  	uint8_t len;
 	uint8_t bits[11];
};

/* Short opcodes for the c46 */
static const struct seeprom_cmd seeprom_ewen = {9, {1, 0, 0, 1, 1, 0, 0, 0, 0}};
static const struct seeprom_cmd seeprom_ewds = {9, {1, 0, 0, 0, 0, 0, 0, 0, 0}};

/* Long opcodes for the C56/C66 */
static const struct seeprom_cmd seeprom_long_ewen = {11, {1, 0, 0, 1, 1, 0, 0, 0, 0}};
static const struct seeprom_cmd seeprom_long_ewds = {11, {1, 0, 0, 0, 0, 0, 0, 0, 0}};

/* Common opcodes */
static const struct seeprom_cmd seeprom_write = {3, {1, 0, 1}};
static const struct seeprom_cmd seeprom_read  = {3, {1, 1, 0}};

/*
 * Wait for the SEERDY to go high; about 800 ns.
 */
#define CLOCK_PULSE(sd, rdy)				\
	while ((SEEPROM_STATUS_INB(sd) & rdy) == 0) {	\
		;  /* Do nothing */			\
	}						\
	(void)SEEPROM_INB(sd);	/* Clear clock */

/*
 * Send a START condition and the given command
 */
static void
send_seeprom_cmd(struct seeprom_descriptor *sd, const struct seeprom_cmd *cmd)
{
	uint8_t temp;
	int i = 0;

	/* Send chip select for one clock cycle. */
	temp = sd->sd_MS ^ sd->sd_CS;
	SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
	CLOCK_PULSE(sd, sd->sd_RDY);

	for (i = 0; i < cmd->len; i++) {
		if (cmd->bits[i] != 0)
			temp ^= sd->sd_DO;
		SEEPROM_OUTB(sd, temp);
		CLOCK_PULSE(sd, sd->sd_RDY);
		SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
		CLOCK_PULSE(sd, sd->sd_RDY);
		if (cmd->bits[i] != 0)
			temp ^= sd->sd_DO;
	}
}

/*
 * Clear CS put the chip in the reset state, where it can wait for new commands.
 */
static void
reset_seeprom(struct seeprom_descriptor *sd)
{
	uint8_t temp;

	temp = sd->sd_MS;
	SEEPROM_OUTB(sd, temp);
	CLOCK_PULSE(sd, sd->sd_RDY);
	SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
	CLOCK_PULSE(sd, sd->sd_RDY);
	SEEPROM_OUTB(sd, temp);
	CLOCK_PULSE(sd, sd->sd_RDY);
}

/*
 * Read the serial EEPROM and returns 1 if successful and 0 if
 * not successful.
 */
int
ahc_read_seeprom(struct seeprom_descriptor *sd, uint16_t *buf,
		 u_int start_addr, u_int count)
{
	int i = 0;
	u_int k = 0;
	uint16_t v;
	uint8_t temp;

	/*
	 * Read the requested registers of the seeprom.  The loop
	 * will range from 0 to count-1.
	 */
	for (k = start_addr; k < count + start_addr; k++) {
		/*
		 * Now we're ready to send the read command followed by the
		 * address of the 16-bit register we want to read.
		 */
		send_seeprom_cmd(sd, &seeprom_read);

		/* Send the 6 or 8 bit address (MSB first, LSB last). */
		temp = sd->sd_MS ^ sd->sd_CS;
		for (i = (sd->sd_chip - 1); i >= 0; i--) {
			if ((k & (1 << i)) != 0)
				temp ^= sd->sd_DO;
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
			if ((k & (1 << i)) != 0)
				temp ^= sd->sd_DO;
		}

		/*
		 * Now read the 16 bit register.  An initial 0 precedes the
		 * register contents which begins with bit 15 (MSB) and ends
		 * with bit 0 (LSB).  The initial 0 will be shifted off the
		 * top of our word as we let the loop run from 0 to 16.
		 */
		v = 0;
		for (i = 16; i >= 0; i--) {
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			v <<= 1;
			if (SEEPROM_DATA_INB(sd) & sd->sd_DI)
				v |= 1;
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
		}

		buf[k - start_addr] = v;

		/* Reset the chip select for the next command cycle. */
		reset_seeprom(sd);
	}
#ifdef AHC_DUMP_EEPROM
	printf("\nSerial EEPROM:\n\t");
	for (k = 0; k < count; k = k + 1) {
		if (((k % 8) == 0) && (k != 0)) {
			printf ("\n\t");
		}
		printf (" 0x%x", buf[k]);
	}
	printf ("\n");
#endif
	return (1);
}

/*
 * Write the serial EEPROM and return 1 if successful and 0 if
 * not successful.
 */
int
ahc_write_seeprom(struct seeprom_descriptor *sd, uint16_t *buf,
		  u_int start_addr, u_int count)
{
	const struct seeprom_cmd *ewen, *ewds;
	uint16_t v;
	uint8_t temp;
	int i, k;

	/* Place the chip into write-enable mode */
	if (sd->sd_chip == C46) {
		ewen = &seeprom_ewen;
		ewds = &seeprom_ewds;
	} else if (sd->sd_chip == C56_66) {
		ewen = &seeprom_long_ewen;
		ewds = &seeprom_long_ewds;
	} else {
		printf("ahc_write_seeprom: unsupported seeprom type %d\n",
		       sd->sd_chip);
		return (0);
	}

	send_seeprom_cmd(sd, ewen);
	reset_seeprom(sd);

	/* Write all requested data out to the seeprom. */
	temp = sd->sd_MS ^ sd->sd_CS;
	for (k = start_addr; k < count + start_addr; k++) {
		/* Send the write command */
		send_seeprom_cmd(sd, &seeprom_write);

		/* Send the 6 or 8 bit address (MSB first). */
		for (i = (sd->sd_chip - 1); i >= 0; i--) {
			if ((k & (1 << i)) != 0)
				temp ^= sd->sd_DO;
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
			if ((k & (1 << i)) != 0)
				temp ^= sd->sd_DO;
		}

		/* Write the 16 bit value, MSB first */
		v = buf[k - start_addr];
		for (i = 15; i >= 0; i--) {
			if ((v & (1 << i)) != 0)
				temp ^= sd->sd_DO;
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
			if ((v & (1 << i)) != 0)
				temp ^= sd->sd_DO;
		}

		/* Wait for the chip to complete the write */
		temp = sd->sd_MS;
		SEEPROM_OUTB(sd, temp);
		CLOCK_PULSE(sd, sd->sd_RDY);
		temp = sd->sd_MS ^ sd->sd_CS;
		do {
			SEEPROM_OUTB(sd, temp);
			CLOCK_PULSE(sd, sd->sd_RDY);
			SEEPROM_OUTB(sd, temp ^ sd->sd_CK);
			CLOCK_PULSE(sd, sd->sd_RDY);
		} while ((SEEPROM_DATA_INB(sd) & sd->sd_DI) == 0);

		reset_seeprom(sd);
	}

	/* Put the chip back into write-protect mode */
	send_seeprom_cmd(sd, ewds);
	reset_seeprom(sd);

	return (1);
}

int
ahc_verify_cksum(struct seeprom_config *sc)
{
	int i;
	int maxaddr;
	uint32_t checksum;
	uint16_t *scarray;

	maxaddr = (sizeof(*sc)/2) - 1;
	checksum = 0;
	scarray = (uint16_t *)sc;

	for (i = 0; i < maxaddr; i++)
		checksum = checksum + scarray[i];
	if (checksum == 0
	 || (checksum & 0xFFFF) != sc->checksum) {
		return (0);
	} else {
		return(1);
	}
}
