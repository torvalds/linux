#ifndef __UM_SLIRP_H
#define __UM_SLIRP_H

#define BUF_SIZE 1500
 /* two bytes each for a (pathological) max packet of escaped chars +  * 
  * terminating END char + initial END char                            */
#define ENC_BUF_SIZE (2 * BUF_SIZE + 2)

#define SLIRP_MAX_ARGS 100
/*
 * XXX this next definition is here because I don't understand why this
 * initializer doesn't work in slirp_kern.c:
 *
 *   argv :  { init->argv[ 0 ... SLIRP_MAX_ARGS-1 ] },
 *
 * or why I can't typecast like this:
 *
 *   argv :  (char* [SLIRP_MAX_ARGS])(init->argv), 
 */
struct arg_list_dummy_wrapper { char *argv[SLIRP_MAX_ARGS]; };

struct slirp_data {
	void *dev;
	struct arg_list_dummy_wrapper argw;
	int pid;
	int slave;
	char ibuf[ENC_BUF_SIZE];
	char obuf[ENC_BUF_SIZE];
	int more; /* more data: do not read fd until ibuf has been drained */
	int pos;
	int esc;
};

extern struct net_user_info slirp_user_info;

extern int set_umn_addr(int fd, char *addr, char *ptp_addr);
extern int slirp_user_read(int fd, void *buf, int len, struct slirp_data *pri);
extern int slirp_user_write(int fd, void *buf, int len, struct slirp_data *pri);

#endif

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
