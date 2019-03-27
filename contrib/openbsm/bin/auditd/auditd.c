/*-
 * Copyright (c) 2004-2009 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <config/config.h>

#include <sys/dirent.h>
#ifdef HAVE_FULL_QUEUE_H
#include <sys/queue.h>
#else	/* !HAVE_FULL_QUEUE_H */
#include <compat/queue.h>
#endif	/* !HAVE_FULL_QUEUE_H */
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <bsm/audit.h>
#include <bsm/audit_uevents.h>
#include <bsm/auditd_lib.h>
#include <bsm/libbsm.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "auditd.h"

#ifndef HAVE_STRLCPY
#include <compat/strlcpy.h>
#endif

/*
 * XXX The following are temporary until these can be added to the kernel
 * audit.h header.
 */
#ifndef	AUDIT_TRIGGER_INITIALIZE
#define	AUDIT_TRIGGER_INITIALIZE	7
#endif
#ifndef	AUDIT_TRIGGER_EXPIRE_TRAILS
#define	AUDIT_TRIGGER_EXPIRE_TRAILS	8
#endif


/*
 * LaunchD flag (Mac OS X and, maybe, FreeBSD only.)  See launchd(8) and
 * http://wiki.freebsd.org/launchd for more information.
 *
 *	In order for auditd to work "on demand" with launchd(8) it can't:
 *		call daemon(3)
 *		call fork and having the parent process exit
 *		change uids or gids.
 *		set up the current working directory or chroot.
 *		set the session id
 *		change stdio to /dev/null.
 *		call setrusage(2)
 *		call setpriority(2)
 *		Ignore SIGTERM.
 *	auditd (in 'launchd mode') is launched on demand so it must catch
 *	SIGTERM to exit cleanly.
 */
static int	launchd_flag = 0;

/*
 * The GID of the audit review group (if used).  The audit trail files and
 * system logs (Mac OS X only) can only be reviewed by members of this group
 * or the audit administrator (aka. "root").
 */
static gid_t	audit_review_gid = -1;

/*
 * The path and file name of the last audit trail file.
 */
static char	*lastfile = NULL;

/*
 * Error starting auditd. Run warn script and exit.
 */
static void
fail_exit(void)
{

	audit_warn_nostart();
	exit(1);
}

/*
 * Follow the 'current' symlink to get the active trail file name.
 */
static char *
get_curfile(void)
{
	char *cf;
	int len;

	cf = malloc(MAXPATHLEN);
	if (cf == NULL) {
		auditd_log_err("malloc failed: %m");
		return (NULL);
	}

	len = readlink(AUDIT_CURRENT_LINK, cf, MAXPATHLEN - 1);
	if (len < 0) {
		free(cf);
		return (NULL);
	}

	/* readlink() doesn't terminate string. */
	cf[len] = '\0';

	return (cf);
}

/*
 * Close the previous audit trail file.
 */
static int
close_lastfile(char *TS)
{
	char *ptr;
	char *oldname;

	/* If lastfile is NULL try to get it from the 'current' link.  */
	if (lastfile == NULL)
		lastfile = get_curfile();

	if (lastfile != NULL) {
		oldname = strdup(lastfile);
		if (oldname == NULL)
			return (-1);

		/* Rename the last file -- append timestamp. */
		if ((ptr = strstr(lastfile, NOT_TERMINATED)) != NULL) {
			memcpy(ptr, TS, POSTFIX_LEN);
			if (auditd_rename(oldname, lastfile) != 0)
				auditd_log_err(
				    "Could not rename %s to %s: %m", oldname,
				    lastfile);
			else {
				/*
				 * Remove the 'current' symlink since the link
				 * is now invalid.
				 */
				(void) unlink(AUDIT_CURRENT_LINK);
				auditd_log_notice("renamed %s to %s",
				    oldname, lastfile);
				audit_warn_closefile(lastfile);
			}
		} else
			auditd_log_err("Could not rename %s to %s", oldname,
			    lastfile);
		free(lastfile);
		free(oldname);
		lastfile = NULL;
	}
	return (0);
}

/*
 * Create the new file name, swap with existing audit file.
 */
static int
swap_audit_file(void)
{
	int err;
	char *newfile, *name;
	char TS[TIMESTAMP_LEN + 1];
	time_t tt;

	if (getTSstr(tt, TS, sizeof(TS)) != 0)
		return (-1);
	/*
	 * If prefix and suffix are the same, it means that records are
	 * being produced too fast. We don't want to rename now, because
	 * next trail file can get the same name and once that one is
	 * terminated also within one second it will overwrite the current
	 * one. Just keep writing to the same trail and wait for the next
	 * trigger from the kernel.
	 * FREEBSD KERNEL WAS UPDATED TO KEEP SENDING TRIGGERS, WHICH MIGHT
	 * NOT BE THE CASE FOR OTHER OSES.
	 * If the kernel will not keep sending triggers, trail file will not
	 * be terminated.
	 */
	if (lastfile == NULL) {
		name = NULL;
	} else {
		name = strrchr(lastfile, '/');
		if (name != NULL)
			name++;
	}
	if (name != NULL && strncmp(name, TS, TIMESTAMP_LEN) == 0) {
		auditd_log_debug("Not ready to terminate trail file yet.");
		return (0);
	}
	err = auditd_swap_trail(TS, &newfile, audit_review_gid,
	    audit_warn_getacdir);
	if (err != ADE_NOERR) {
		auditd_log_err("%s: %m", auditd_strerror(err));
		if (err != ADE_ACTL)
			return (-1);
	}

	/*
	 * Only close the last file if were in an auditing state before
	 * calling swap_audit_file().  We may need to recover from a crash.
	 */
	if (auditd_get_state() == AUD_STATE_ENABLED)
		close_lastfile(TS);


	/*
	 * auditd_swap_trail() potentially enables auditing (if not already
	 * enabled) so updated the cached state as well.
	 */
	auditd_set_state(AUD_STATE_ENABLED);

	/*
	 *  Create 'current' symlink.  Recover from crash, if needed.
	 */
	if (auditd_new_curlink(newfile) != 0)
		auditd_log_err("auditd_new_curlink(\"%s\") failed: %s: %m",
		    newfile, auditd_strerror(err));

	lastfile = newfile;
	auditd_log_notice("New audit file is %s", newfile);

	return (0);
}

/*
 * Create a new audit log trail file and swap with the current one, if any.
 */
static int
do_trail_file(void)
{
	int err;

	/*
	 * First, refresh the list of audit log directories.
	 */
	err = auditd_read_dirs(audit_warn_soft, audit_warn_hard);
	if (err) {
		auditd_log_err("auditd_read_dirs(): %s",
		    auditd_strerror(err));
		if (err == ADE_HARDLIM)
			audit_warn_allhard();
		if (err != ADE_SOFTLIM)
			return (-1);
		else
			audit_warn_allsoft();
			/* continue on with soft limit error */
	}

	/*
	 * Create a new file and swap with the one being used in kernel.
	 */
	if (swap_audit_file() == -1) {
		/*
		 * XXX Faulty directory listing? - user should be given
		 * XXX an opportunity to change the audit_control file
		 * XXX switch to a reduced mode of auditing?
		 */
		return (-1);
	}

	/*
	 * Finally, see if there are any trail files to expire.
	 */
	err = auditd_expire_trails(audit_warn_expired);
	if (err)
		auditd_log_err("auditd_expire_trails(): %s",
		    auditd_strerror(err));

	return (0);
}

/*
 * Start up auditing.
 */
static void
audit_setup(void)
{
	int err;

	/* Configure trail files distribution. */
	err = auditd_set_dist();
	if (err) {
		auditd_log_err("auditd_set_dist() %s: %m",
		    auditd_strerror(err));
	} else
		auditd_log_debug("Configured trail files distribution.");

	if (do_trail_file() == -1) {
		auditd_log_err("Error creating audit trail file");
		fail_exit();
	}

	/* Generate an audit record. */
	err = auditd_gen_record(AUE_audit_startup, NULL);
	if (err)
		auditd_log_err("auditd_gen_record(AUE_audit_startup) %s: %m",
		    auditd_strerror(err));

	if (auditd_config_controls() == 0)
		auditd_log_info("Audit controls init successful");
	else
		auditd_log_err("Audit controls init failed");
}


/*
 * Close auditd pid file and trigger mechanism.
 */
static int
close_misc(void)
{

	auditd_close_dirs();
	if (unlink(AUDITD_PIDFILE) == -1 && errno != ENOENT) {
		auditd_log_err("Couldn't remove %s: %m", AUDITD_PIDFILE);
		return (1);
	}
	endac();

	if (auditd_close_trigger() != 0) {
		auditd_log_err("Error closing trigger messaging mechanism");
		return (1);
	}
	return (0);
}

/*
 * Close all log files, control files, and tell the audit system.
 */
static int
close_all(void)
{
	int err_ret = 0;
	char TS[TIMESTAMP_LEN + 1];
	int err;
	int cond;
	time_t tt;

	err = auditd_gen_record(AUE_audit_shutdown, NULL);
	if (err)
		auditd_log_err("auditd_gen_record(AUE_audit_shutdown) %s: %m",
		    auditd_strerror(err));

	/* Flush contents. */
	cond = AUC_DISABLED;
	err_ret = audit_set_cond(&cond);
	if (err_ret != 0) {
		auditd_log_err("Disabling audit failed! : %s", strerror(errno));
		err_ret = 1;
	}

	/*
	 * Updated the cached state that auditing has been disabled.
	 */
	auditd_set_state(AUD_STATE_DISABLED);

	if (getTSstr(tt, TS, sizeof(TS)) == 0)
		close_lastfile(TS);
	if (lastfile != NULL)
		free(lastfile);

	err_ret += close_misc();

	if (err_ret) {
		auditd_log_err("Could not unregister");
		audit_warn_postsigterm();
	}

	auditd_log_info("Finished");
	return (err_ret);
}

/*
 * Register the daemon with the signal handler and the auditd pid file.
 */
static int
register_daemon(void)
{
	struct sigaction action;
	FILE * pidfile;
	int fd;
	pid_t pid;

	/* Set up the signal hander. */
	action.sa_handler = auditd_relay_signal;
	/*
	 * sa_flags must not include SA_RESTART, so that read(2) will be
	 * interruptible in auditd_wait_for_events
	 */
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTERM, &action, NULL) != 0) {
		auditd_log_err(
		    "Could not set signal handler for SIGTERM");
		fail_exit();
	}
	if (sigaction(SIGCHLD, &action, NULL) != 0) {
		auditd_log_err(
		    "Could not set signal handler for SIGCHLD");
		fail_exit();
	}
	if (sigaction(SIGHUP, &action, NULL) != 0) {
		auditd_log_err(
		    "Could not set signal handler for SIGHUP");
		fail_exit();
	}
	if (sigaction(SIGALRM, &action, NULL) != 0) {
		auditd_log_err(
		    "Could not set signal handler for SIGALRM");
		fail_exit();
	}

	if ((pidfile = fopen(AUDITD_PIDFILE, "a")) == NULL) {
		auditd_log_err("Could not open PID file");
		audit_warn_tmpfile();
		return (-1);
	}

	/* Attempt to lock the pid file; if a lock is present, exit. */
	fd = fileno(pidfile);
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		auditd_log_err(
		    "PID file is locked (is another auditd running?).");
		audit_warn_ebusy();
		return (-1);
	}

	pid = getpid();
	ftruncate(fd, 0);
	if (fprintf(pidfile, "%u\n", pid) < 0) {
		/* Should not start the daemon. */
		fail_exit();
	}

	fflush(pidfile);
	return (0);
}

/*
 * Handle the audit trigger event.
 *
 * We suppress (ignore) duplicated triggers in close succession in order to
 * try to avoid thrashing-like behavior.  However, not all triggers can be
 * ignored, as triggers generally represent edge triggers, not level
 * triggers, and won't be retransmitted if the condition persists.  Of
 * specific concern is the rotate trigger -- if one is dropped, then it will
 * not be retransmitted, and the log file will grow in an unbounded fashion.
 */
#define	DUPLICATE_INTERVAL	30
void
auditd_handle_trigger(int trigger)
{
	static int last_trigger, last_warning;
	static time_t last_time;
	struct timeval ts;
	struct timezone tzp;
	time_t tt;
	int au_state;
	int err = 0;

	/*
	 * Suppress duplicate messages from the kernel within the specified
	 * interval.
	 */
	if (gettimeofday(&ts, &tzp) == 0) {
		tt = (time_t)ts.tv_sec;
		switch (trigger) {
		case AUDIT_TRIGGER_LOW_SPACE:
		case AUDIT_TRIGGER_NO_SPACE:
			/*
			 * Triggers we can suppress.  Of course, we also need
			 * to rate limit the warnings, so apply the same
			 * interval limit on syslog messages.
			 */
			if ((trigger == last_trigger) &&
			    (tt < (last_time + DUPLICATE_INTERVAL))) {
				if (tt >= (last_warning + DUPLICATE_INTERVAL))
					auditd_log_info(
					    "Suppressing duplicate trigger %d",
					    trigger);
				return;
			}
			last_warning = tt;
			break;

		case AUDIT_TRIGGER_ROTATE_KERNEL:
		case AUDIT_TRIGGER_ROTATE_USER:
		case AUDIT_TRIGGER_READ_FILE:
		case AUDIT_TRIGGER_CLOSE_AND_DIE:
		case AUDIT_TRIGGER_INITIALIZE:
			/*
			 * Triggers that we cannot suppress.
			 */
			break;
		}

		/*
		 * Only update last_trigger after aborting due to a duplicate
		 * trigger, not before, or we will never allow that trigger
		 * again.
		 */
		last_trigger = trigger;
		last_time = tt;
	}

	au_state = auditd_get_state();

	/*
	 * Message processing is done here.
	 */
	switch(trigger) {
	case AUDIT_TRIGGER_LOW_SPACE:
		auditd_log_notice("Got low space trigger");
		if (do_trail_file() == -1)
			auditd_log_err("Error swapping audit file");
		break;

	case AUDIT_TRIGGER_NO_SPACE:
		auditd_log_notice("Got no space trigger");
		if (do_trail_file() == -1)
			auditd_log_err("Error swapping audit file");
		break;

	case AUDIT_TRIGGER_ROTATE_KERNEL:
	case AUDIT_TRIGGER_ROTATE_USER:
		auditd_log_info("Got open new trigger from %s", trigger ==
		    AUDIT_TRIGGER_ROTATE_KERNEL ? "kernel" : "user");
		if (au_state == AUD_STATE_ENABLED && do_trail_file() == -1)
			auditd_log_err("Error swapping audit file");
		break;

	case AUDIT_TRIGGER_READ_FILE:
		auditd_log_info("Got read file trigger");
		if (au_state == AUD_STATE_ENABLED) {
			if (auditd_config_controls() == -1)
				auditd_log_err("Error setting audit controls");
			else if (do_trail_file() == -1)
				auditd_log_err("Error swapping audit file");
		}
		break;

	case AUDIT_TRIGGER_CLOSE_AND_DIE:
		auditd_log_info("Got close and die trigger");
		if (au_state == AUD_STATE_ENABLED)
			err = close_all();
		/*
		 * Running under launchd don't exit.  Wait for launchd to
		 * send SIGTERM.
		 */
		if (!launchd_flag) {
			auditd_log_info("auditd exiting.");
			exit (err);
		}
		break;

	case AUDIT_TRIGGER_INITIALIZE:
		auditd_log_info("Got audit initialize trigger");
		if (au_state == AUD_STATE_DISABLED)
			audit_setup();
		break;

	case AUDIT_TRIGGER_EXPIRE_TRAILS:
		auditd_log_info("Got audit expire trails trigger");
		err = auditd_expire_trails(audit_warn_expired);
		if (err)
			auditd_log_err("auditd_expire_trails(): %s",
			    auditd_strerror(err));
		break;

	default:
		auditd_log_err("Got unknown trigger %d", trigger);
		break;
	}
}

/*
 * Reap our children.
 */
void
auditd_reap_children(void)
{
	pid_t child;
	int wstatus;

	while ((child = waitpid(-1, &wstatus, WNOHANG)) > 0) {
		if (!wstatus)
			continue;
		auditd_log_info("warn process [pid=%d] %s %d.", child,
		    ((WIFEXITED(wstatus)) ? "exited with non-zero status" :
		    "exited as a result of signal"),
		    ((WIFEXITED(wstatus)) ? WEXITSTATUS(wstatus) :
		    WTERMSIG(wstatus)));
	}
}

/*
 * Reap any children and terminate.  If under launchd don't shutdown auditing
 * but just the other stuff.
 */
void
auditd_terminate(void)
{
	int ret;

	auditd_reap_children();

	if (launchd_flag)
		ret = close_misc();
	else
		ret = close_all();

	exit(ret);
}

/*
 * Configure the audit controls in the kernel: the event to class mapping,
 * kernel preselection mask, etc.
 */
int
auditd_config_controls(void)
{
	int cnt, err;
	int ret = 0;

	/*
	 * Configure event to class mappings in kernel.
	 */
	cnt = auditd_set_evcmap();
	if (cnt < 0) {
		auditd_log_err("auditd_set_evcmap() failed: %m");
		ret = -1;
	} else if (cnt == 0) {
		auditd_log_err("No events to class mappings registered.");
		ret = -1;
	} else
		auditd_log_debug("Registered %d event to class mappings.", cnt);

	/*
	 * Configure non-attributable event mask in kernel.
	 */
	err = auditd_set_namask();
	if (err) {
		auditd_log_err("auditd_set_namask() %s: %m",
		    auditd_strerror(err));
		ret = -1;
	} else
		auditd_log_debug("Registered non-attributable event mask.");

	/*
	 * Configure audit policy in kernel.
	 */
	err = auditd_set_policy();
	if (err) {
		auditd_log_err("auditd_set_policy() %s: %m",
		    auditd_strerror(err));
		ret = -1;
	} else
		auditd_log_debug("Set audit policy in kernel.");

	/*
	 * Configure audit trail log size in kernel.
	 */
	err = auditd_set_fsize();
	if (err) {
		auditd_log_err("audit_set_fsize() %s: %m",
		    auditd_strerror(err));
		ret = -1;
	} else
		auditd_log_debug("Set audit trail size in kernel.");

	/*
	 * Configure audit trail queue size in kernel.
	 */
	err = auditd_set_qsize();
	if (err) {
		auditd_log_err("audit_set_qsize() %s: %m",
		    auditd_strerror(err));
		ret = -1;
	} else
		auditd_log_debug("Set audit trail queue in kernel.");

	/*
	 * Configure audit trail volume minimum free percentage of blocks in
	 * kernel.
	 */
	err = auditd_set_minfree();
	if (err) {
		auditd_log_err("auditd_set_minfree() %s: %m",
		    auditd_strerror(err));
		ret = -1;
	} else
		auditd_log_debug(
		    "Set audit trail min free percent in kernel.");

	/*
	 * Configure host address in the audit kernel information.
	 */
	err = auditd_set_host();
	if (err) {
		if (err == ADE_PARSE) {
			auditd_log_notice(
			    "audit_control(5) may be missing 'host:' field");
		} else {
			auditd_log_err("auditd_set_host() %s: %m",
			    auditd_strerror(err));
			ret = -1;
		}
	} else
		auditd_log_debug(
		    "Set audit host address information in kernel.");

	return (ret);
}

/*
 * Setup and initialize auditd.
 */
static void
setup(void)
{
	int err;

	if (auditd_open_trigger(launchd_flag) < 0) {
		auditd_log_err("Error opening trigger messaging mechanism");
		fail_exit();
	}

	/*
	 * To prevent event feedback cycles and avoid auditd becoming
	 * stalled if auditing is suspended, auditd and its children run
	 * without their events being audited.  We allow the uid, tid, and
	 * mask fields to be implicitly set to zero, but do set the pid.  We
	 * run this after opening the trigger device to avoid configuring
	 * audit state without audit present in the system.
	 */
	err = auditd_prevent_audit();
	if (err) {
		auditd_log_err("auditd_prevent_audit() %s: %m",
		    auditd_strerror(err));
		fail_exit();
	}

	/*
	 * Make sure auditd auditing state is correct.
	 */
	auditd_set_state(AUD_STATE_INIT);

	/*
	 * If under launchd, don't start auditing.  Wait for a trigger to
	 * do so.
	 */
	if (!launchd_flag)
		audit_setup();
}

int
main(int argc, char **argv)
{
	int ch;
	int debug = 0;
#ifdef AUDIT_REVIEW_GROUP
	struct group *grp;
#endif

	while ((ch = getopt(argc, argv, "dl")) != -1) {
		switch(ch) {
		case 'd':
			/* Debug option. */
			debug = 1;
			break;

		case 'l':
			/* Be launchd friendly. */
			launchd_flag = 1;
			break;

		case '?':
		default:
			(void)fprintf(stderr,
			    "usage: auditd [-d] [-l]\n");
			exit(1);
		}
	}

	audit_review_gid = getgid();

#ifdef AUDIT_REVIEW_GROUP
	/*
	 * XXXRW: Currently, this code falls back to the daemon gid, which is
	 * likely the wheel group.  Is there a better way to deal with this?
	 */
	grp = getgrnam(AUDIT_REVIEW_GROUP);
	if (grp != NULL)
		audit_review_gid = grp->gr_gid;
#endif

	auditd_openlog(debug, audit_review_gid);

	if (launchd_flag)
		auditd_log_info("started by launchd...");
	else
		auditd_log_info("starting...");

#ifdef AUDIT_REVIEW_GROUP
	if (grp == NULL)
		auditd_log_info(
		    "Audit review group '%s' not available, using daemon gid (%d)",
		    AUDIT_REVIEW_GROUP, audit_review_gid);
#endif
	if (debug == 0 && launchd_flag == 0 && daemon(0, 0) == -1) {
		auditd_log_err("Failed to daemonize");
		exit(1);
	}

	if (register_daemon() == -1) {
		auditd_log_err("Could not register as daemon");
		exit(1);
	}

	setup();

	/*
	 * auditd_wait_for_events() shouldn't return unless something is wrong.
	 */
	auditd_wait_for_events();

	auditd_log_err("abnormal exit.");
	close_all();
	exit(-1);
}
