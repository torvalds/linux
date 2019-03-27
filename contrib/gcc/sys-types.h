enum clnt_stat { ___fake1 };
enum auth_stat { ___fake2 };

struct netconfig;
struct netbuf;
struct address;
struct tm;
struct ldfile;
struct syment;
struct stat;
struct timeval;
struct termios;
struct tms;
struct dma_cb;
struct cred;
struct vnode;
struct vattr;
struct uarg;
struct statfs;
struct statvfs;
struct dirent;
struct itimerval;
struct mnttab;
struct strbuf;
struct vfstab;
struct ldfile;
struct syment;
struct scnhdr;
struct exception;
struct nd_hostservlist;
struct nd_hostserv;
struct utsname;
struct uio;
struct pid;
struct pollfd;
struct nlist;
struct passwd;
struct spwd;
struct flock;
struct seg;
struct sembuf;
struct sigaction;
struct utimbuf;
struct map;
struct filehdr;
struct lineno;
struct nd_addrlist;
struct FTW;
struct buf;
struct ustat;
struct qelem;
struct prpsinfo;
struct user;
struct qelem;
struct execenv;
struct utmpx;

struct direct;
struct tm;
struct stat;
struct rlimit;
struct rusage;
struct sockaddr;
struct sockaddr_in;
struct timeval { int i; };
struct exportent;
struct fstab;
struct hostent;
struct in_addr { int i; };
struct ldfile;
struct mallinfo { int i; };
struct mint;
struct nmtent;
struct netent;
struct pmaplist;
struct protoent;
struct rpcent;
struct servent;
struct authdes_cred;
struct rpc_err;
struct ypall_callback;

union wait;

/* Get size_t and wchar_t.  */
#include <stddef.h>

/* #include "sys/types.h" */
#define ssize_t int

/* The actual types used here are mostly wrong,
   but it is not supposed to matter what types we use here.  */

typedef int dev_t;
typedef int pid_t;
typedef int gid_t;
typedef int off_t;
typedef int mode_t;
typedef int uid_t;

typedef int proc_t;
typedef int time_t;
typedef int addr_t;
typedef int caddr_t;
typedef int clock_t;
typedef int div_t;
typedef int ldiv_t;
typedef int dl_t;
typedef int major_t;
typedef int minor_t;
typedef int emcp_t;
typedef int fpclass_t;
typedef int index_t;
typedef int ecb_t;
typedef int aioop_t;
typedef int evver_t;
typedef int evcntlcmds_t;
typedef int idtype_t;
typedef int id_t;
typedef int procset_t;
typedef int hostid_t;
typedef int evpollcmds_t;
typedef int event_t;
typedef int hrtime_t;
typedef int evqcntlcmds_t;
typedef int sigset_t;
typedef int evsiginfo_t;
typedef int evcontext_t;
typedef int evta_t;
typedef int speed_t;
typedef int rlim_t;
typedef int cred_t;
typedef int file_t;
typedef int vnode_t;
typedef int vfs_t;
typedef int fpos_t;
typedef int exhda_t;
typedef int ucontext_t;
typedef int sess_t;
typedef int hrtcmd_t;
typedef int interval_t;
typedef int key_t;
typedef int daddr_t;
typedef int stack_t;
typedef int sigaction_t;
typedef int siginfo_t;
typedef int mblk_t;
typedef int paddr_t;
typedef int qband_t;
typedef int queue_t;
typedef int rf_resource_t;
typedef int sr_mount_t;
typedef int timer_t;
typedef int fpregset_t;
typedef int prstatus_t;
typedef int vfssw_t;
typedef int eucwidth_t;
typedef int page_t;

typedef int u_int;
typedef int u_short;
typedef int u_long;
typedef int u_char;

typedef int ushort;
typedef int ulong;
typedef int uint;

typedef int __gnuc_va_list;

typedef int archdr;
typedef int AUTH;
typedef int CLIENT;
typedef int DIR;
typedef int ENTRY;
typedef int Elf;
typedef int Elf32_Ehdr;
typedef int Elf32_Phdr;
typedef int Elf32_Shdr;
typedef int Elf_Arhdr;
typedef int Elf_Arsym;
typedef int Elf_Cmd;
typedef int Elf_Data;
typedef int Elf_Scn;
typedef int Elf_Type;
typedef int Elf_Kind;
typedef int FIELD;
typedef int FIELDTYPE;
typedef int PTF_int;
typedef int PTF_void;
typedef int PTF_charP;
typedef int FILE;
typedef int FORM;
typedef int ITEM;
typedef int MENU;
typedef int OPTIONS;
typedef int PANEL;
typedef int FTP_void;
typedef int RPCBLIST;
typedef int SCREEN;
typedef int SVCXPRT;
typedef int TERMINAL;
typedef int WINDOW;
typedef int bool;
typedef int nl_catd;
typedef int nl_item;
typedef int chtype;
typedef int datum;
typedef int fp_rnd;
typedef int spraycumul;
typedef int WORD;
typedef int VISIT;
typedef int ACTION;

typedef int *jmp_buf;
typedef int *sigjmp_buf;
typedef int xdrproc_t;
typedef int CALL;
typedef int bool_t;
typedef int DBM;
typedef int des_block;
typedef int resultproc_t;


#ifdef BSD

#define mode_t int
#define uid_t int
#define gid_t int
#define time_t long
#define pid_t int
#define signal_ret_t int
#define wait_arg_t union wait

#else

#define signal_ret_t void
#define wait_arg_t int

#endif
