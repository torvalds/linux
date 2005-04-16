#ifndef __INCLUDED_TEA6420__
#define __INCLUDED_TEA6420__

/* possible addresses */
#define	I2C_TEA6420_1		0x4c
#define	I2C_TEA6420_2		0x4d

struct tea6420_multiplex
{
	int	in;	/* input of audio switch */
	int	out;	/* output of audio switch  */
	int	gain;	/* gain of connection */
};

#define TEA6420_SWITCH		_IOW('v',1,struct tea6420_multiplex)

#endif
