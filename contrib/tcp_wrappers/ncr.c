 /*
  * This part for NCR UNIX with is from Andrew Maffei (arm@aqua.whoi.edu). It
  * assumes TLI throughout. In order to look up endpoint address information
  * we must talk to the "timod" streams module. For some reason "timod" wants
  * to sit directly on top of the device driver. Therefore we pop off all
  * streams modules except the driver, install the "timod" module so that we
  * can figure out network addresses, and then restore the original state.
  */

#ifndef lint
static char sccsid[] = "@(#) ncr.c 1.1 94/12/28 17:42:34";
#endif

#include <sys/types.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/tiuser.h>
#include <stropts.h>
#include <sys/conf.h>

#include "tcpd.h"

#define MAX_MODULE_COUNT	10	/* XXX */

/* fromhost - tear down the streams stack then rebuild it */

void    fromhost(request)
struct request_info *request;
{
    int     i;
    int     num_mod;
    struct str_list str_list;
    struct str_mlist mod_buffer[MAX_MODULE_COUNT];
    int     fd = request->fd;

    str_list.sl_nmods = MAX_MODULE_COUNT;
    str_list.sl_modlist = &mod_buffer[0];

    /*
     * On systems with WIN streams support we have to be careful about what
     * is on the stream we are passed. This code POPs off all modules above
     * the pseudo driver, pushes timod, gets the host address information,
     * pops timod and then pushes all modules back on the stream.
     * 
     * Some state may be lost in this process. /usr/etc/tlid seems to do special
     * things to the stream depending on the TCP port being serviced. (not a
     * very nice thing to do!). It is unclear what to do if this code breaks
     * - the stream may be left in an unknown condition.
     */
    if ((num_mod = ioctl(fd, I_LIST, NULL)) < 0)
	tcpd_warn("fromhost: LIST failed: %m");
    if (ioctl(fd, I_LIST, &str_list) < 0)
	tcpd_warn("fromhost: LIST failed: %m");

    /*
     * POP stream modules except for the driver.
     */
    for (i = 0; i < num_mod - 1; i++)
	if (ioctl(fd, I_POP, 0) < 0)
	    tcpd_warn("fromhost: POP %s: %m", mod_buffer[i].l_name);

    /*
     * PUSH timod so that host address ioctls can be executed.
     */
    if (ioctl(fd, I_PUSH, "timod") < 0)
	tcpd_warn("fromhost: PUSH timod: %m");
    tli_host(request);

    /*
     * POP timod, we're done with it now.
     */
    if (ioctl(fd, I_POP, 0) < 0)
	tcpd_warn("fromhost: POP timod: %m");

    /*
     * Restore stream modules.
     */
    for (i = num_mod - 2; i >= 0; i--)
	if (ioctl(fd, I_PUSH, mod_buffer[i].l_name) < 0)
	    tcpd_warn("fromhost: PUSH %s: %m", mod_buffer[i].l_name);
}
