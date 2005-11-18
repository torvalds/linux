/* 
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include "kern_util.h"
#include "chan_user.h"
#include "user_util.h"
#include "user.h"
#include "os.h"
#include "xterm.h"

struct xterm_chan {
	int pid;
	int helper_pid;
	char *title;
	int device;
	int raw;
	struct termios tt;
	unsigned long stack;
	int direct_rcv;
};

/* Not static because it's called directly by the tt mode gdb code */
void *xterm_init(char *str, int device, struct chan_opts *opts)
{
	struct xterm_chan *data;

	data = malloc(sizeof(*data));
	if(data == NULL) return(NULL);
	*data = ((struct xterm_chan) { .pid 		= -1, 
				       .helper_pid 	= -1,
				       .device 		= device, 
				       .title 		= opts->xterm_title,
				       .raw  		= opts->raw,
				       .stack 		= opts->tramp_stack,
				       .direct_rcv 	= !opts->in_kernel } );
	return(data);
}

/* Only changed by xterm_setup, which is a setup */
static char *terminal_emulator = "xterm";
static char *title_switch = "-T";
static char *exec_switch = "-e";

static int __init xterm_setup(char *line, int *add)
{
	*add = 0;
	terminal_emulator = line;

	line = strchr(line, ',');
	if(line == NULL) return(0);
	*line++ = '\0';
	if(*line) title_switch = line;

	line = strchr(line, ',');
	if(line == NULL) return(0);
	*line++ = '\0';
	if(*line) exec_switch = line;

	return(0);
}

__uml_setup("xterm=", xterm_setup,
"xterm=<terminal emulator>,<title switch>,<exec switch>\n"
"    Specifies an alternate terminal emulator to use for the debugger,\n"
"    consoles, and serial lines when they are attached to the xterm channel.\n"
"    The values are the terminal emulator binary, the switch it uses to set\n"
"    its title, and the switch it uses to execute a subprocess,\n"
"    respectively.  The title switch must have the form '<switch> title',\n"
"    not '<switch>=title'.  Similarly, the exec switch must have the form\n"
"    '<switch> command arg1 arg2 ...'.\n"
"    The default values are 'xterm=xterm,-T,-e'.  Values for gnome-terminal\n"
"    are 'xterm=gnome-terminal,-t,-x'.\n\n"
);

/* XXX This badly needs some cleaning up in the error paths
 * Not static because it's called directly by the tt mode gdb code
 */
int xterm_open(int input, int output, int primary, void *d,
		      char **dev_out)
{
	struct xterm_chan *data = d;
	unsigned long stack;
	int pid, fd, new, err;
	char title[256], file[] = "/tmp/xterm-pipeXXXXXX";
	char *argv[] = { terminal_emulator, title_switch, title, exec_switch, 
			 "/usr/lib/uml/port-helper", "-uml-socket",
			 file, NULL };

	if(os_access(argv[4], OS_ACC_X_OK) < 0)
		argv[4] = "port-helper";

	/* Check that DISPLAY is set, this doesn't guarantee the xterm
	 * will work but w/o it we can be pretty sure it won't. */
	if (!getenv("DISPLAY")) {
		printk("xterm_open: $DISPLAY not set.\n");
		return -ENODEV;
	}

	fd = mkstemp(file);
	if(fd < 0){
		err = -errno;
		printk("xterm_open : mkstemp failed, errno = %d\n", errno);
		return err;
	}

	if(unlink(file)){
		err = -errno;
		printk("xterm_open : unlink failed, errno = %d\n", errno);
		return err;
	}
	os_close_file(fd);

	fd = os_create_unix_socket(file, sizeof(file), 1);
	if(fd < 0){
		printk("xterm_open : create_unix_socket failed, errno = %d\n", 
		       -fd);
		return(fd);
	}

	sprintf(title, data->title, data->device);
	stack = data->stack;
	pid = run_helper(NULL, NULL, argv, &stack);
	if(pid < 0){
		printk("xterm_open : run_helper failed, errno = %d\n", -pid);
		return(pid);
	}

	if(data->stack == 0) free_stack(stack, 0);

	if (data->direct_rcv) {
		new = os_rcv_fd(fd, &data->helper_pid);
	} else {
		err = os_set_fd_block(fd, 0);
		if(err < 0){
			printk("xterm_open : failed to set descriptor "
			       "non-blocking, err = %d\n", -err);
			return(err);
		}
		new = xterm_fd(fd, &data->helper_pid);
	}
	if(new < 0){
		printk("xterm_open : os_rcv_fd failed, err = %d\n", -new);
		goto out;
	}

	CATCH_EINTR(err = tcgetattr(new, &data->tt));
	if(err){
		new = err;
		goto out;
	}

	if(data->raw){
		err = raw(new);
		if(err){
			new = err;
			goto out;
		}
	}

	data->pid = pid;
	*dev_out = NULL;
 out:
	unlink(file);
	return(new);
}

/* Not static because it's called directly by the tt mode gdb code */
void xterm_close(int fd, void *d)
{
	struct xterm_chan *data = d;
	
	if(data->pid != -1) 
		os_kill_process(data->pid, 1);
	data->pid = -1;
	if(data->helper_pid != -1) 
		os_kill_process(data->helper_pid, 0);
	data->helper_pid = -1;
	os_close_file(fd);
}

static void xterm_free(void *d)
{
	free(d);
}

struct chan_ops xterm_ops = {
	.type		= "xterm",
	.init		= xterm_init,
	.open		= xterm_open,
	.close		= xterm_close,
	.read		= generic_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= xterm_free,
	.winch		= 1,
};

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
