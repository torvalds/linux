|
|	binstr.sa 3.3 12/19/90
|
|
|	Description: Converts a 64-bit binary integer to bcd.
|
|	Input: 64-bit binary integer in d2:d3, desired length (LEN) in
|          d0, and a  pointer to start in memory for bcd characters
|          in d0. (This pointer must point to byte 4 of the first
|          lword of the packed decimal memory string.)
|
|	Output:	LEN bcd digits representing the 64-bit integer.
|
|	Algorithm:
|		The 64-bit binary is assumed to have a decimal point before
|		bit 63.  The fraction is multiplied by 10 using a mul by 2
|		shift and a mul by 8 shift.  The bits shifted out of the
|		msb form a decimal digit.  This process is iterated until
|		LEN digits are formed.
|
|	A1. Init d7 to 1.  D7 is the byte digit counter, and if 1, the
|		digit formed will be assumed the least significant.  This is
|		to force the first byte formed to have a 0 in the upper 4 bits.
|
|	A2. Beginning of the loop:
|		Copy the fraction in d2:d3 to d4:d5.
|
|	A3. Multiply the fraction in d2:d3 by 8 using bit-field
|		extracts and shifts.  The three msbs from d2 will go into
|		d1.
|
|	A4. Multiply the fraction in d4:d5 by 2 using shifts.  The msb
|		will be collected by the carry.
|
|	A5. Add using the carry the 64-bit quantities in d2:d3 and d4:d5
|		into d2:d3.  D1 will contain the bcd digit formed.
|
|	A6. Test d7.  If zero, the digit formed is the ms digit.  If non-
|		zero, it is the ls digit.  Put the digit in its place in the
|		upper word of d0.  If it is the ls digit, write the word
|		from d0 to memory.
|
|	A7. Decrement d6 (LEN counter) and repeat the loop until zero.
|
|	Implementation Notes:
|
|	The registers are used as follows:
|
|		d0: LEN counter
|		d1: temp used to form the digit
|		d2: upper 32-bits of fraction for mul by 8
|		d3: lower 32-bits of fraction for mul by 8
|		d4: upper 32-bits of fraction for mul by 2
|		d5: lower 32-bits of fraction for mul by 2
|		d6: temp for bit-field extracts
|		d7: byte digit formation word;digit count {0,1}
|		a0: pointer into memory for packed bcd string formation
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|       For details on the license for this file, please see the
|       file, README, in this same directory.

|BINSTR    idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

#include "fpsp.h"

	.global	binstr
binstr:
	moveml	%d0-%d7,-(%a7)
|
| A1: Init d7
|
	moveql	#1,%d7			|init d7 for second digit
	subql	#1,%d0			|for dbf d0 would have LEN+1 passes
|
| A2. Copy d2:d3 to d4:d5.  Start loop.
|
loop:
	movel	%d2,%d4			|copy the fraction before muls
	movel	%d3,%d5			|to d4:d5
|
| A3. Multiply d2:d3 by 8; extract msbs into d1.
|
	bfextu	%d2{#0:#3},%d1		|copy 3 msbs of d2 into d1
	asll	#3,%d2			|shift d2 left by 3 places
	bfextu	%d3{#0:#3},%d6		|copy 3 msbs of d3 into d6
	asll	#3,%d3			|shift d3 left by 3 places
	orl	%d6,%d2			|or in msbs from d3 into d2
|
| A4. Multiply d4:d5 by 2; add carry out to d1.
|
	asll	#1,%d5			|mul d5 by 2
	roxll	#1,%d4			|mul d4 by 2
	swap	%d6			|put 0 in d6 lower word
	addxw	%d6,%d1			|add in extend from mul by 2
|
| A5. Add mul by 8 to mul by 2.  D1 contains the digit formed.
|
	addl	%d5,%d3			|add lower 32 bits
	nop				|ERRATA ; FIX #13 (Rev. 1.2 6/6/90)
	addxl	%d4,%d2			|add with extend upper 32 bits
	nop				|ERRATA ; FIX #13 (Rev. 1.2 6/6/90)
	addxw	%d6,%d1			|add in extend from add to d1
	swap	%d6			|with d6 = 0; put 0 in upper word
|
| A6. Test d7 and branch.
|
	tstw	%d7			|if zero, store digit & to loop
	beqs	first_d			|if non-zero, form byte & write
sec_d:
	swap	%d7			|bring first digit to word d7b
	aslw	#4,%d7			|first digit in upper 4 bits d7b
	addw	%d1,%d7			|add in ls digit to d7b
	moveb	%d7,(%a0)+		|store d7b byte in memory
	swap	%d7			|put LEN counter in word d7a
	clrw	%d7			|set d7a to signal no digits done
	dbf	%d0,loop		|do loop some more!
	bras	end_bstr		|finished, so exit
first_d:
	swap	%d7			|put digit word in d7b
	movew	%d1,%d7			|put new digit in d7b
	swap	%d7			|put LEN counter in word d7a
	addqw	#1,%d7			|set d7a to signal first digit done
	dbf	%d0,loop		|do loop some more!
	swap	%d7			|put last digit in string
	lslw	#4,%d7			|move it to upper 4 bits
	moveb	%d7,(%a0)+		|store it in memory string
|
| Clean up and return with result in fp0.
|
end_bstr:
	moveml	(%a7)+,%d0-%d7
	rts
	|end
