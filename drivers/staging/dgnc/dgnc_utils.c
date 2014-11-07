#include <linux/tty.h>
#include <linux/sched.h>
#include "dgnc_utils.h"
#include "digi.h"

/*
 * dgnc_ms_sleep()
 *
 * Put the driver to sleep for x ms's
 *
 * Returns 0 if timed out, !0 (showing signal) if interrupted by a signal.
 */
int dgnc_ms_sleep(ulong ms)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout((ms * HZ) / 1000);
	return signal_pending(current);
}

/*
 *      dgnc_ioctl_name() : Returns a text version of each ioctl value.
 */
char *dgnc_ioctl_name(int cmd)
{
	switch (cmd) {

	case TCGETA:		return "TCGETA";
	case TCGETS:		return "TCGETS";
	case TCSETA:		return "TCSETA";
	case TCSETS:		return "TCSETS";
	case TCSETAW:		return "TCSETAW";
	case TCSETSW:		return "TCSETSW";
	case TCSETAF:		return "TCSETAF";
	case TCSETSF:		return "TCSETSF";
	case TCSBRK:		return "TCSBRK";
	case TCXONC:		return "TCXONC";
	case TCFLSH:		return "TCFLSH";
	case TIOCGSID:		return "TIOCGSID";

	case TIOCGETD:		return "TIOCGETD";
	case TIOCSETD:		return "TIOCSETD";
	case TIOCGWINSZ:	return "TIOCGWINSZ";
	case TIOCSWINSZ:	return "TIOCSWINSZ";

	case TIOCMGET:		return "TIOCMGET";
	case TIOCMSET:		return "TIOCMSET";
	case TIOCMBIS:		return "TIOCMBIS";
	case TIOCMBIC:		return "TIOCMBIC";

	/* from digi.h */
	case DIGI_SETA:		return "DIGI_SETA";
	case DIGI_SETAW:	return "DIGI_SETAW";
	case DIGI_SETAF:	return "DIGI_SETAF";
	case DIGI_SETFLOW:	return "DIGI_SETFLOW";
	case DIGI_SETAFLOW:	return "DIGI_SETAFLOW";
	case DIGI_GETFLOW:	return "DIGI_GETFLOW";
	case DIGI_GETAFLOW:	return "DIGI_GETAFLOW";
	case DIGI_GETA:		return "DIGI_GETA";
	case DIGI_GEDELAY:	return "DIGI_GEDELAY";
	case DIGI_SEDELAY:	return "DIGI_SEDELAY";
	case DIGI_GETCUSTOMBAUD: return "DIGI_GETCUSTOMBAUD";
	case DIGI_SETCUSTOMBAUD: return "DIGI_SETCUSTOMBAUD";
	case TIOCMODG:		return "TIOCMODG";
	case TIOCMODS:		return "TIOCMODS";
	case TIOCSDTR:		return "TIOCSDTR";
	case TIOCCDTR:		return "TIOCCDTR";

	default:		return "unknown";
	}
}
