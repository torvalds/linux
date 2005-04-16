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
#include "slip_proto.h"
#include "helper.h"
#include "os.h"

void slip_user_init(void *data, void *dev)
{
	struct slip_data *pri = data;

	pri->dev = dev;
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
		return(err);
	}

	err = 0;
	pe_data.stdin = fd;
	pe_data.stdout = fds[1];
	pe_data.close_me = fds[0];
	pid = run_helper(slip_pre_exec, &pe_data, argv, NULL);

	if(pid < 0) err = pid;
	else {
		output_len = page_size();
		output = um_kmalloc(output_len);
		if(output == NULL)
			printk("slip_tramp : failed to allocate output "
			       "buffer\n");

		os_close_file(fds[1]);
		read_output(fds[0], output, output_len);
		if(output != NULL){
			printk("%s", output);
			kfree(output);
		}
		CATCH_EINTR(err = waitpid(pid, &status, 0));
		if(err < 0)
			err = errno;
		else if(!WIFEXITED(status) || (WEXITSTATUS(status) != 0)){
			printk("'%s' didn't exit with status 0\n", argv[0]);
			err = -EINVAL;
		}
	}

	os_close_file(fds[0]);

	return(err);
}

static int slip_open(void *data)
{
	struct slip_data *pri = data;
	char version_buf[sizeof("nnnnn\0")];
	char gate_buf[sizeof("nnn.nnn.nnn.nnn\0")];
	char *argv[] = { "uml_net", version_buf, "slip", "up", gate_buf, 
			 NULL };
	int sfd, mfd, err;

	mfd = get_pty();
	if(mfd < 0){
		printk("umn : Failed to open pty, err = %d\n", -mfd);
		return(mfd);
	}
	sfd = os_open_file(ptsname(mfd), of_rdwr(OPENFLAGS()), 0);
	if(sfd < 0){
		printk("Couldn't open tty for slip line, err = %d\n", -sfd);
		os_close_file(mfd);
		return(sfd);
	}
	if(set_up_tty(sfd)) return(-1);
	pri->slave = sfd;
	pri->pos = 0;
	pri->esc = 0;
	if(pri->gate_addr != NULL){
		sprintf(version_buf, "%d", UML_NET_VERSION);
		strcpy(gate_buf, pri->gate_addr);

		err = slip_tramp(argv, sfd);

		if(err < 0){
			printk("slip_tramp failed - err = %d\n", -err);
			return(err);
		}
		err = os_get_ifname(pri->slave, pri->name);
		if(err < 0){
			printk("get_ifname failed, err = %d\n", -err);
			return(err);
		}
		iter_addresses(pri->dev, open_addr, pri->name);
	}
	else {
		err = os_set_slip(sfd);
		if(err < 0){
			printk("Failed to set slip discipline encapsulation - "
			       "err = %d\n", -err);
			return(err);
		}
	}
	return(mfd);
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
	int i, n, size, start;

	if(pri->more>0) {
		i = 0;
		while(i < pri->more) {
			size = slip_unesc(pri->ibuf[i++],
					pri->ibuf, &pri->pos, &pri->esc);
			if(size){
				memcpy(buf, pri->ibuf, size);
				memmove(pri->ibuf, &pri->ibuf[i], pri->more-i);
				pri->more=pri->more-i; 
				return(size);
			}
		}
		pri->more=0;
	}

	n = net_read(fd, &pri->ibuf[pri->pos], sizeof(pri->ibuf) - pri->pos);
	if(n <= 0) return(n);

	start = pri->pos;
	for(i = 0; i < n; i++){
		size = slip_unesc(pri->ibuf[start + i],
				pri->ibuf, &pri->pos, &pri->esc);
		if(size){
			memcpy(buf, pri->ibuf, size);
			memmove(pri->ibuf, &pri->ibuf[start+i+1], n-(i+1));
			pri->more=n-(i+1); 
			return(size);
		}
	}
	return(0);
}

int slip_user_write(int fd, void *buf, int len, struct slip_data *pri)
{
	int actual, n;

	actual = slip_esc(buf, pri->obuf, len);
	n = net_write(fd, pri->obuf, actual);
	if(n < 0) return(n);
	else return(len);
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

struct net_user_info slip_user_info = {
	.init		= slip_user_init,
	.open		= slip_open,
	.close	 	= slip_close,
	.remove	 	= NULL,
	.set_mtu	= slip_set_mtu,
	.add_address	= slip_add_addr,
	.delete_address = slip_del_addr,
	.max_packet	= BUF_SIZE
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
