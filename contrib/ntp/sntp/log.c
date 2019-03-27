#include <config.h>

#include "log.h"

const char *progname;		/* for msyslog use too */

static int counter = 0;

static void cleanup_log(void);

void
sntp_init_logging(
	const char *prog
	)
{
	
	msyslog_term = TRUE;
	init_logging(prog, 0, FALSE);
	msyslog_term_pid = FALSE;
	msyslog_include_timestamp = FALSE;
}


void
open_logfile(
	const char *logfile
	)
{
	change_logfile(logfile, FALSE);
	counter = 1; //counter++;
	atexit(cleanup_log);
}

//not sure about this. Are the atexit() functions called by FIFO or LIFO order? The end result is PROBABLY the same
static void
cleanup_log(void)
{
	//counter--;
	//if(counter <= 0){
	if(counter == 1){
		syslogit = TRUE;
		fflush(syslog_file);
		fclose(syslog_file);
		syslog_file = NULL;
		counter = 0;
	}
}
