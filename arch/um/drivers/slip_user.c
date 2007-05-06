#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <sched.h>
#include <string.h>
#include <errno.h>
#include <sys/termios.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include "user_util.h"
#include "kern_util.h"
#include "user.h"
#include "net_user.h"
#include "slip.h"
#include "slip_common.h"
#include "os.h"
#include "um_malloc.h"

static int slip_user_init(void *data, void *dev)
{
	struct slip_data *pri = data;

	pri->dev = dev;
	return 0;
}

static int set_up_tty(int fd)
{
	int i;
	struct termios tios;

	if (tcgetattr(fd, &tios) < 0) {
		printk("could not get initial terminal attributes\n");
		return(-1);
	}

	tios.c_cflag = CS8 | CREAD | HUPCL | CLOCAL;
	tios.c_iflag = IGNBRK | IGNPAR;
	tios.c_oflag = 0;
	tios.c_lflag = 0;
	for (i = 0; i < NCCS; i++)
		tios.c_cc[i] = 0;
	tios.c_cc[VMIN] = 1;
	tios.c_cc[VTIME] = 0;

	cfsetospeed(&tios, B38400);
	cfsetispeed(&tios, B38400);

	if (tcsetattr(fd, TCSAFLUSH, &tios) < 0) {
		printk("failed to set terminal attributes\n");
		return(-1);
	}
	return(0);
}

struct slip_pre_exec_data {
	int stdin;
	int stdout;
	int close_me;
};

static void slip_pre_exec(void *arg)
{
	struct slip_pre_exec_data *data = arg;

	if(data->stdin >= 0) dup2(data->stdin, 0);
	dup2(data->stdout, 1);
	if(data->close_me >= 0) os_close_file(data->close_me);
}

static int slip_tramp(char **argv, int fd)
{
	struct slip_pre_exec_data pe_data;
	char *output;
	int status, pid, fds[2], err, output_len;

	err = os_pipe(fds, 1, 0);
	if(err < 0){
		printk("slip_tramp : pipe failed, err = %d\n", -err);
		goto out;
	}

	err = 0;
	pe_data.stdin = fd;
	pe_data.stdout = fds[1];
	pe_data.close_me = fds[0];
	err = run_helper(slip_pre_exec, &pe_data, argv, NULL);
	if(err < 0)
		goto out_close;
	pid = err;

	output_len = page_size();
	output = um_kmalloc(output_len);
	if(output == NULL){
		printk("slip_tramp : failed to allocate output buffer\n");
		os_kill_process(pid, 1);
		err = -ENOMEM;
		goto out_free;
	}

	os_close_file(fds[1]);
	read_output(fds[0], output, output_len);
	printk("%s", output);

	CATCH_EINTR(err = waitpid(pid, &status, 0));
	if(err < 0)
		err = errno;
	else if(!WIFEXITED(status) || (WEXITSTATUS(status) != 0)){
		printk("'%s' didn't exit with status 0\n", argv[0]);
		err = -EINVAL;
	}
	else err = 0;

	os_close_file(fds[0]);

out_free:
	kfree(output);
	return err;

out_close:
	os_close_file(fds[0]);
	os_close_file(fds[1]);
out:
	return err;
}

static int slip_open(void *data)
{
	struct slip_data *pri = data;
	char version_buf[sizeof("nnnnn\0")];
	char gate_buf[sizeof("nnn.nnn.nnn.nnn\0")];
	char *argv[] = { "uml_net", version_buf, "slip", "up", gate_buf, 
			 NULL };
	int sfd, mfd, err;

	err = get_pty();
	if(err < 0){
		printk("slip-open : Failed to open pty, err = %d\n", -err);
		goto out;
	}
	mfd = err;

	err = os_open_file(ptsname(mfd), of_rdwr(OPENFLAGS()), 0);
	if(err < 0){
		printk("Couldn't open tty for slip line, err = %d\n", -err);
		goto out_close;
	}
	sfd = err;

	if(set_up_tty(sfd))
		goto out_close2;

	pri->slave = sfd;
	pri->slip.pos = 0;
	pri->slip.esc = 0;
	if(pri->gate_addr != NULL){
		sprintf(version_buf, "%d", UML_NET_VERSION);
		strcpy(gate_buf, pri->gate_addr);

		err = slip_tramp(argv, sfd);

		if(err < 0){
			printk("slip_tramp failed - err = %d\n", -err);
			goto out_close2;
		}
		err = os_get_ifname(pri->slave, pri->name);
		if(err < 0){
			printk("get_ifname failed, err = %d\n", -err);
			goto out_close2;
		}
		iter_addresses(pri->dev, open_addr, pri->name);
	}
	else {
		err = os_set_slip(sfd);
		if(err < 0){
			printk("Failed to set slip discipline encapsulation - "
			       "err = %d\n", -err);
			goto out_close2;
		}
	}
	return(mfd);
out_close2:
	os_close_file(sfd);
out_close:
	os_close_file(mfd);
out:
	return err;
}

static void slip_close(int fd, void *data)
{
	struct slip_data *pri = data;
	char version_buf[sizeof("nnnnn\0")];
	char *argv[] = { "uml_net", version_buf, "slip", "down", pri->name, 
			 NULL };
	int err;

	if(pri->gate_addr != NULL)
		iter_addresses(pri->dev, close_addr, pri->name);

	sprintf(version_buf, "%d", UML_NET_VERSION);

	err = slip_tramp(argv, pri->slave);

	if(err != 0)
		printk("slip_tramp failed - errno = %d\n", -err);
	os_close_file(fd);
	os_close_file(pri->slave);
	pri->slave = -1;
}

int slip_user_read(int fd, void *buf, int len, struct slip_data *pri)
{
	return slip_proto_read(fd, buf, len, &pri->slip);
}

int slip_user_write(int fd, void *buf, int len, struct slip_data *pri)
{
	return slip_proto_write(fd, buf, len, &pri->slip);
}

static int slip_set_mtu(int mtu, void *data)
{
	return(mtu);
}

static void slip_add_addr(unsigned char *addr, unsigned char *netmask,
			  void *data)
{
	struct slip_data *pri = data;

	if(pri->slave < 0) return;
	open_addr(addr, netmask, pri->name);
}

static void slip_del_addr(unsigned char *addr, unsigned char *netmask,
			    void *data)
{
	struct slip_data *pri = data;

	if(pri->slave < 0) return;
	close_addr(addr, netmask, pri->name);
}

const struct net_user_info slip_user_info = {
	.init		= slip_user_init,
	.open		= slip_open,
	.close	 	= slip_close,
	.remove	 	= NULL,
	.set_mtu	= slip_set_mtu,
	.add_address	= slip_add_addr,
	.delete_address = slip_del_addr,
	.max_packet	= BUF_SIZE
};
