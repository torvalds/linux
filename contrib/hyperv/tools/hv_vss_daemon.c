#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#include <sys/types.h>

#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <stdint.h>
#include <syslog.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <ufs/ffs/fs.h>
#include <paths.h>
#include <sysexits.h>

#include "hv_snapshot.h"

#define UNDEF_FREEZE_THAW       (0)
#define FREEZE                  (1)
#define THAW                    (2)

#define	VSS_LOG(priority, format, args...) do	{				\
		if (is_debugging == 1) {					\
			if (is_daemon == 1)					\
				syslog(priority, format, ## args);		\
			else							\
				printf(format, ## args);			\
		} else {							\
			if (priority < LOG_DEBUG) {				\
				if (is_daemon == 1)				\
					syslog(priority, format, ## args);	\
				else						\
					printf(format, ## args);		\
			}							\
		}								\
	} while(0)

static int is_daemon = 1;
static int is_debugging = 0;
static int g_ufs_suspend_handle = -1;

static const char *dev = "/dev";

static int
check(void)
{
	struct statfs *mntbuf, *statfsp;
	int mntsize;
	int i;

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	if (mntsize == 0) {
		VSS_LOG(LOG_ERR, "There is no mount information\n");
		return (EINVAL);
	}
	for (i = mntsize - 1; i >= 0; --i)
	{
		statfsp = &mntbuf[i];

		if (strncmp(statfsp->f_mntonname, dev, strlen(dev)) == 0) {
			continue; /* skip to freeze '/dev' */
		} else if (statfsp->f_flags & MNT_RDONLY) {
			continue; /* skip to freeze RDONLY partition */
		} else if (strncmp(statfsp->f_fstypename, "ufs", 3) != 0) {
			return (EPERM); /* only UFS can be freezed */
		}
	}

	return (0);
}

static int
freeze(void)
{
	struct statfs *mntbuf, *statfsp;
	int mntsize;
	int error = 0;
	int i;

	g_ufs_suspend_handle = open(_PATH_UFSSUSPEND, O_RDWR);
	if (g_ufs_suspend_handle == -1) {
		VSS_LOG(LOG_ERR, "unable to open %s", _PATH_UFSSUSPEND);
		return (errno);
	}

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);
	if (mntsize == 0) {
		VSS_LOG(LOG_ERR, "There is no mount information\n");
		return (EINVAL);
	}
	for (i = mntsize - 1; i >= 0; --i)
	{
		statfsp = &mntbuf[i];

		if (strncmp(statfsp->f_mntonname, dev, strlen(dev)) == 0) {
			continue; /* skip to freeze '/dev' */
		} else if (statfsp->f_flags & MNT_RDONLY) {
			continue; /* skip to freeze RDONLY partition */
		} else if (strncmp(statfsp->f_fstypename, "ufs", 3) != 0) {
			continue; /* only UFS can be freezed */
		}
		error = ioctl(g_ufs_suspend_handle, UFSSUSPEND, &statfsp->f_fsid);
		if (error != 0) {
			VSS_LOG(LOG_ERR, "error: %d\n", errno);
			error = errno;
		} else {
			VSS_LOG(LOG_INFO, "Successfully suspend fs: %s\n",
			    statfsp->f_mntonname);
		}
	}

	return (error);
}

/**
 * close the opened handle will thaw the FS.
 */
static int
thaw(void)
{
	int error = 0;
	if (g_ufs_suspend_handle != -1) {
		error = close(g_ufs_suspend_handle);
		if (!error) {
			g_ufs_suspend_handle = -1;
			VSS_LOG(LOG_INFO, "Successfully thaw the fs\n");
		} else {
			error = errno;
			VSS_LOG(LOG_ERR, "Fail to thaw the fs: "
			    "%d %s\n", errno, strerror(errno));
		}
	} else {
		VSS_LOG(LOG_INFO, "The fs has already been thawed\n");
	}

	return (error);
}

static void
usage(const char* cmd)
{
	fprintf(stderr, "%s: daemon for UFS file system freeze/thaw\n"
	    " -d : enable debug log printing. Default is disabled.\n"
	    " -n : run as a regular process instead of a daemon. Default is a daemon.\n"
	    " -h : print usage.\n", cmd);
	exit(1);
}

int
main(int argc, char* argv[])
{
	struct hv_vss_opt_msg  userdata;

	struct pollfd hv_vss_poll_fd[1];
	uint32_t op;
	int ch, r, error;
	int hv_vss_dev_fd;

	while ((ch = getopt(argc, argv, "dnh")) != -1) {
		switch (ch) {
		case 'n':
			/* Run as regular process for debugging purpose. */
			is_daemon = 0;
			break;
		case 'd':
			/* Generate debugging output */
			is_debugging = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}
	}

	openlog("HV_VSS", 0, LOG_USER);

	/* Become daemon first. */
	if (is_daemon == 1)
		daemon(1, 0);
	else
		VSS_LOG(LOG_DEBUG, "Run as regular process.\n");

	VSS_LOG(LOG_INFO, "HV_VSS starting; pid is: %d\n", getpid());

	memset(&userdata, 0, sizeof(struct hv_vss_opt_msg));
	/* register the daemon */
	hv_vss_dev_fd = open(VSS_DEV(FS_VSS_DEV_NAME), O_RDWR);

	if (hv_vss_dev_fd < 0) {
		VSS_LOG(LOG_ERR, "Fail to open %s, error: %d %s\n",
		    VSS_DEV(FS_VSS_DEV_NAME), errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	hv_vss_poll_fd[0].fd = hv_vss_dev_fd;
	hv_vss_poll_fd[0].events = POLLIN | POLLRDNORM;

	while (1) {
		r = poll(hv_vss_poll_fd, 1, INFTIM);

		VSS_LOG(LOG_DEBUG, "poll returned r = %d, revent = 0x%x\n",
		    r, hv_vss_poll_fd[0].revents);

		if (r == 0 || (r < 0 && errno == EAGAIN) ||
		    (r < 0 && errno == EINTR)) {
			/* Nothing to read */
			continue;
		}

		if (r < 0) {
			/*
			 * For poll return failure other than EAGAIN,
			 * we want to exit.
			 */
			VSS_LOG(LOG_ERR, "Poll failed.\n");
			perror("poll");
			exit(EIO);
		}

		/* Read from character device */
		error = ioctl(hv_vss_dev_fd, IOCHVVSSREAD, &userdata);
		if (error < 0) {
			VSS_LOG(LOG_ERR, "Read failed.\n");
			perror("pread");
			exit(EIO);
		}

		if (userdata.status != 0) {
			VSS_LOG(LOG_ERR, "data read error\n");
			continue;
		}

		/*
		 * We will use the KVP header information to pass back
		 * the error from this daemon. So, first save the op
		 * and pool info to local variables.
		 */

		op = userdata.opt;

		switch (op) {
		case HV_VSS_CHECK:
			error = check();
			break;
		case HV_VSS_FREEZE:
			error = freeze();
			break;
		case HV_VSS_THAW:
			error = thaw();
			break;
		default:
			VSS_LOG(LOG_ERR, "Illegal operation: %d\n", op);
			error = VSS_FAIL;
		}
		if (error)
			userdata.status = VSS_FAIL;
		else
			userdata.status = VSS_SUCCESS;
		error = ioctl(hv_vss_dev_fd, IOCHVVSSWRITE, &userdata);
		if (error != 0) {
			VSS_LOG(LOG_ERR, "Fail to write to device\n");
			exit(EXIT_FAILURE);
		} else {
			VSS_LOG(LOG_INFO, "Send response %d for %s to kernel\n",
			    userdata.status, op == HV_VSS_FREEZE ? "Freeze" :
			    (op == HV_VSS_THAW ? "Thaw" : "Check"));
		}
	}
}
