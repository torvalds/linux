/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * 32-bit IRIX5 ABI system call table derived from original file 'irix5sys.h'
 * created by David S. Miller.
 *
 * Copyright (C) 1996 - 2004 David S. Miller <dm@engr.sgi.com>
 * Copyright (C) 2004 Steven J. Hill <sjhill@realitydiluted.com>
 */
#include <asm/asm.h>

	/*
	 * Key:
	 *         V == Valid and should work as expected for most cases.
	 *        HV == Half Valid, some things will work, some likely will not
	 *        IV == InValid, certainly will not work at all yet
	 *        ?V == ?'ably Valid, I have not done enough looking into it
	 *        DC == Don't Care, a rats ass we couldn't give
	 */

	.macro	irix5syscalltable

	sys	sys_syscall	0	/* 1000  sysindir()	       V*/
	sys	sys_exit	1	/* 1001  exit()		       V*/
	sys	sys_fork	0	/* 1002  fork()		       V*/
	sys	sys_read	3	/* 1003  read()		       V*/
	sys	sys_write	3	/* 1004  write()	       V*/
	sys	sys_open	3	/* 1005  open()		       V*/
	sys	sys_close	1	/* 1006  close()	       V*/
	sys	irix_unimp	0	/* 1007  (XXX IRIX 4 wait)     V*/
	sys	sys_creat	2	/* 1008  creat()	       V*/
	sys	sys_link	2	/* 1009  link()		       V*/
	sys	sys_unlink	1	/* 1010  unlink()	       V*/
	sys	irix_exec	0	/* 1011  exec()		       V*/
	sys	sys_chdir	1	/* 1012  chdir()	       V*/
	sys	irix_gtime	0	/* 1013  time()		       V*/
	sys	irix_unimp	0	/* 1014  (XXX IRIX 4 mknod)    V*/
	sys	sys_chmod	2	/* 1015  chmod()	       V*/
	sys	sys_chown	3	/* 1016  chown()	       V*/
	sys	irix_brk	1	/* 1017  break()	       V*/
	sys	irix_unimp	0	/* 1018  (XXX IRIX 4 stat)     V*/
	sys	sys_lseek	3	/* 1019  lseek()     XXX64bit HV*/
	sys	irix_getpid	0	/* 1020  getpid()	       V*/
	sys	irix_mount	6	/* 1021  mount()	      IV*/
	sys	sys_umount	1	/* 1022  umount()	       V*/
	sys	sys_setuid	1	/* 1023  setuid()	       V*/
	sys	irix_getuid	0	/* 1024  getuid()	       V*/
	sys	irix_stime	1	/* 1025  stime()	       V*/
	sys	irix_unimp	4	/* 1026  XXX ptrace()	      IV*/
	sys	irix_alarm	1	/* 1027  alarm()	       V*/
	sys	irix_unimp	0	/* 1028  (XXX IRIX 4 fstat)    V*/
	sys	irix_pause	0	/* 1029  pause()	       V*/
	sys	sys_utime	2	/* 1030  utime()	       V*/
	sys	irix_unimp	0	/* 1031  nuthin'	       V*/
	sys	irix_unimp	0	/* 1032  nobody home man...    V*/
	sys	sys_access	2	/* 1033  access()	       V*/
	sys	sys_nice	1	/* 1034  nice()		       V*/
	sys	irix_statfs	2	/* 1035  statfs()	       V*/
	sys	sys_sync	0	/* 1036  sync()		       V*/
	sys	sys_kill	2	/* 1037  kill()		       V*/
	sys	irix_fstatfs	2	/* 1038  fstatfs()	       V*/
	sys	irix_setpgrp	1	/* 1039  setpgrp()	       V*/
	sys	irix_syssgi	0	/* 1040  syssgi()	      HV*/
	sys	sys_dup		1	/* 1041  dup()		       V*/
	sys	sys_pipe	0	/* 1042  pipe()		       V*/
	sys	irix_times	1	/* 1043  times()	       V*/
	sys	irix_unimp	0	/* 1044  XXX profil()	      IV*/
	sys	irix_unimp	0	/* 1045  XXX lock()	      IV*/
	sys	sys_setgid	1	/* 1046  setgid()	       V*/
	sys	irix_getgid	0	/* 1047  getgid()	       V*/
	sys	irix_unimp	0	/* 1048  (XXX IRIX 4 ssig)     V*/
	sys	irix_msgsys	6	/* 1049  sys_msgsys	       V*/
	sys	sys_sysmips	4	/* 1050  sysmips()	      HV*/
	sys	irix_unimp	0	/* 1051	 XXX sysacct()	      IV*/
	sys	irix_shmsys	5	/* 1052  sys_shmsys	       V*/
	sys	irix_semsys	0	/* 1053  sys_semsys	       V*/
	sys	irix_ioctl	3	/* 1054  ioctl()	      HV*/
	sys	irix_uadmin	0	/* 1055  XXX sys_uadmin()     HC*/
	sys	irix_sysmp	0	/* 1056  sysmp()	      HV*/
	sys	irix_utssys	4	/* 1057  sys_utssys()	      HV*/
	sys	irix_unimp	0	/* 1058  nada enchilada	       V*/
	sys	irix_exece	0	/* 1059  exece()	       V*/
	sys	sys_umask	1	/* 1060  umask()	       V*/
	sys	sys_chroot	1	/* 1061  chroot()	       V*/
	sys	irix_fcntl	3	/* 1062  fcntl()	      ?V*/
	sys	irix_ulimit	2	/* 1063  ulimit()	      HV*/
	sys	irix_unimp	0	/* 1064  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1065  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1066  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1067  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1068  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1069  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1070  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1071  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1072  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1073  XXX AFS shit	      DC*/
	sys	irix_unimp	0	/* 1074  nuttin'	       V*/
	sys	irix_unimp	0	/* 1075  XXX sys_getrlimit64()IV*/
	sys	irix_unimp	0	/* 1076  XXX sys_setrlimit64()IV*/
	sys	sys_nanosleep	2	/* 1077  nanosleep()	       V*/
	sys	irix_lseek64	5	/* 1078  lseek64()	      ?V*/
	sys	sys_rmdir	1	/* 1079  rmdir()	       V*/
	sys	sys_mkdir	2	/* 1080  mkdir()	       V*/
	sys	sys_getdents	3	/* 1081  getdents()	       V*/
	sys	irix_sginap	1	/* 1082  sys_sginap()	       V*/
	sys	irix_sgikopt	3	/* 1083  sys_sgikopt()	      DC*/
	sys	sys_sysfs	3	/* 1084  sysfs()	      ?V*/
	sys	irix_unimp	0	/* 1085  XXX sys_getmsg()     DC*/
	sys	irix_unimp	0	/* 1086  XXX sys_putmsg()     DC*/
	sys	sys_poll	3	/* 1087  poll()	               V*/
	sys	irix_sigreturn	0	/* 1088  sigreturn()	      ?V*/
	sys	sys_accept	3	/* 1089  accept()	       V*/
	sys	sys_bind	3	/* 1090  bind()		       V*/
	sys	sys_connect	3	/* 1091  connect()	       V*/
	sys	irix_gethostid	0	/* 1092  sys_gethostid()      ?V*/
	sys	sys_getpeername	3	/* 1093  getpeername()	       V*/
	sys	sys_getsockname	3	/* 1094  getsockname()	       V*/
	sys	sys_getsockopt	5	/* 1095  getsockopt()	       V*/
	sys	sys_listen	2	/* 1096  listen()	       V*/
	sys	sys_recv	4	/* 1097  recv()		       V*/
	sys	sys_recvfrom	6	/* 1098  recvfrom()	       V*/
	sys	sys_recvmsg	3	/* 1099  recvmsg()	       V*/
	sys	sys_select	5	/* 1100  select()	       V*/
	sys	sys_send	4	/* 1101  send()		       V*/
	sys	sys_sendmsg	3	/* 1102  sendmsg()	       V*/
	sys	sys_sendto	6	/* 1103  sendto()	       V*/
	sys	irix_sethostid	1	/* 1104  sys_sethostid()      ?V*/
	sys	sys_setsockopt	5	/* 1105  setsockopt()	       V*/
	sys	sys_shutdown	2	/* 1106  shutdown()	      ?V*/
	sys	irix_socket	3	/* 1107  socket()	       V*/
	sys	sys_gethostname	2	/* 1108  sys_gethostname()    ?V*/
	sys	sys_sethostname	2	/* 1109  sethostname()	      ?V*/
	sys	irix_getdomainname 2	/* 1110  sys_getdomainname()  ?V*/
	sys	sys_setdomainname 2	/* 1111  setdomainname()      ?V*/
	sys	sys_truncate	2	/* 1112  truncate()	       V*/
	sys	sys_ftruncate	2	/* 1113  ftruncate()	       V*/
	sys	sys_rename	2	/* 1114  rename()	       V*/
	sys	sys_symlink	2	/* 1115  symlink()	       V*/
	sys	sys_readlink	3	/* 1116  readlink()	       V*/
	sys	irix_unimp	0	/* 1117  XXX IRIX 4 lstat()   DC*/
	sys	irix_unimp	0	/* 1118  nothin'	       V*/
	sys	irix_unimp	0	/* 1119  XXX nfs_svc()	      DC*/
	sys	irix_unimp	0	/* 1120  XXX nfs_getfh()      DC*/
	sys	irix_unimp	0	/* 1121  XXX async_daemon()   DC*/
	sys	irix_unimp	0	/* 1122  XXX exportfs()	      DC*/
	sys	sys_setregid	2	/* 1123  setregid()	       V*/
	sys	sys_setreuid	2	/* 1124  setreuid()	       V*/
	sys	sys_getitimer	2	/* 1125  getitimer()	       V*/
	sys	sys_setitimer	3	/* 1126  setitimer()	       V*/
	sys	irix_unimp	1	/* 1127  XXX adjtime() 	      IV*/
	sys	irix_gettimeofday 1	/* 1128  gettimeofday()	       V*/
	sys	irix_unimp	0	/* 1129  XXX sproc()	      IV*/
	sys	irix_prctl	0	/* 1130  prctl()	      HV*/
	sys	irix_unimp	0	/* 1131  XXX procblk()	      IV*/
	sys	irix_unimp	0	/* 1132  XXX sprocsp()	      IV*/
	sys	irix_unimp	0	/* 1133  XXX sgigsc()	      IV*/
	sys	irix_mmap32	6	/* 1134  mmap()	   XXXflags?  ?V*/
	sys	sys_munmap	2	/* 1135  munmap()	       V*/
	sys	sys_mprotect	3	/* 1136  mprotect()	       V*/
	sys	sys_msync	4	/* 1137  msync()	       V*/
	sys	irix_madvise	3	/* 1138  madvise()	      DC*/
	sys	irix_pagelock	3	/* 1139  pagelock()	      IV*/
	sys	irix_getpagesize 0	/* 1140  getpagesize()         V*/
	sys	irix_quotactl	0	/* 1141  quotactl()	       V*/
	sys	irix_unimp	0	/* 1142  nobody home man       V*/
	sys	sys_getpgid	1	/* 1143  BSD getpgrp()	       V*/
	sys	irix_BSDsetpgrp 2	/* 1143  BSD setpgrp()	       V*/
	sys	sys_vhangup	0	/* 1144  vhangup()	       V*/
	sys	sys_fsync	1	/* 1145  fsync()	       V*/
	sys	sys_fchdir	1	/* 1146  fchdir()	       V*/
	sys	sys_getrlimit	2	/* 1147  getrlimit()	      ?V*/
	sys	sys_setrlimit	2	/* 1148  setrlimit()	      ?V*/
	sys	sys_cacheflush	3	/* 1150  cacheflush()	      HV*/
	sys	sys_cachectl	3	/* 1151  cachectl()	      HV*/
	sys	sys_fchown	3	/* 1152  fchown()	      ?V*/
	sys	sys_fchmod	2	/* 1153  fchmod()	      ?V*/
	sys	irix_unimp	0	/* 1154  XXX IRIX 4 wait3()    V*/
	sys	sys_socketpair	4	/* 1155  socketpair()	       V*/
	sys	irix_systeminfo	3	/* 1156  systeminfo()	      IV*/
	sys	irix_uname	1	/* 1157  uname()	      IV*/
	sys	irix_xstat	3	/* 1158  xstat()	       V*/
	sys	irix_lxstat	3	/* 1159  lxstat()	       V*/
	sys	irix_fxstat	3	/* 1160  fxstat()	       V*/
	sys	irix_xmknod	0	/* 1161  xmknod()	      ?V*/
	sys	irix_sigaction	4	/* 1162  sigaction()	      ?V*/
	sys	irix_sigpending	1	/* 1163  sigpending()	      ?V*/
	sys	irix_sigprocmask 3	/* 1164  sigprocmask()	      ?V*/
	sys	irix_sigsuspend	0	/* 1165  sigsuspend()	      ?V*/
	sys	irix_sigpoll_sys 3	/* 1166  sigpoll_sys()	      IV*/
	sys	irix_swapctl	2	/* 1167  swapctl()	      IV*/
	sys	irix_getcontext	0	/* 1168  getcontext()	      HV*/
	sys	irix_setcontext	0	/* 1169  setcontext()	      HV*/
	sys	irix_waitsys	5	/* 1170  waitsys()	      IV*/
	sys	irix_sigstack	2	/* 1171  sigstack()	      HV*/
	sys	irix_sigaltstack 2	/* 1172  sigaltstack()	      HV*/
	sys	irix_sigsendset	2	/* 1173  sigsendset()	      IV*/
	sys	irix_statvfs	2	/* 1174  statvfs()	       V*/
	sys	irix_fstatvfs	2	/* 1175  fstatvfs()	       V*/
	sys	irix_unimp	0	/* 1176  XXX getpmsg()	      DC*/
	sys	irix_unimp	0	/* 1177  XXX putpmsg()	      DC*/
	sys	sys_lchown	3	/* 1178  lchown()	       V*/
	sys	irix_priocntl	0	/* 1179  priocntl()	      DC*/
	sys	irix_sigqueue	4	/* 1180  sigqueue()	      IV*/
	sys	sys_readv	3	/* 1181  readv()	       V*/
	sys	sys_writev	3	/* 1182  writev()	       V*/
	sys	irix_truncate64 4	/* 1183  truncate64() XX32bit HV*/
	sys	irix_ftruncate64 4	/* 1184  ftruncate64()XX32bit HV*/
	sys	irix_mmap64	0	/* 1185  mmap64()     XX32bit HV*/
	sys	irix_dmi	0	/* 1186  dmi()		      DC*/
	sys	irix_pread	6	/* 1187  pread()	      IV*/
	sys	irix_pwrite	6	/* 1188  pwrite()	      IV*/
	sys	sys_fsync	1	/* 1189  fdatasync()  XXPOSIX HV*/
	sys	irix_sgifastpath 7	/* 1190  sgifastpath() WHEEE  IV*/
	sys	irix_unimp	0	/* 1191  XXX attr_get()	      DC*/
	sys	irix_unimp	0	/* 1192  XXX attr_getf()      DC*/
	sys	irix_unimp	0	/* 1193  XXX attr_set()	      DC*/
	sys	irix_unimp	0	/* 1194  XXX attr_setf()      DC*/
	sys	irix_unimp	0	/* 1195  XXX attr_remove()    DC*/
	sys	irix_unimp	0	/* 1196  XXX attr_removef()   DC*/
	sys	irix_unimp	0	/* 1197  XXX attr_list()      DC*/
	sys	irix_unimp	0	/* 1198  XXX attr_listf()     DC*/
	sys	irix_unimp	0	/* 1199  XXX attr_multi()     DC*/
	sys	irix_unimp	0	/* 1200  XXX attr_multif()    DC*/
	sys	irix_statvfs64	2	/* 1201  statvfs64()	       V*/
	sys	irix_fstatvfs64	2	/* 1202  fstatvfs64()	       V*/
	sys	irix_getmountid	2	/* 1203  getmountid()XXXfsids HV*/
	sys	irix_nsproc	5	/* 1204  nsproc()	      IV*/
	sys	irix_getdents64 3	/* 1205  getdents64()	      HV*/
	sys	irix_unimp	0	/* 1206  XXX DFS garbage      DC*/
	sys	irix_ngetdents	4	/* 1207  ngetdents() XXXeop   HV*/
	sys	irix_ngetdents64 4	/* 1208  ngetdents64() XXXeop HV*/
	sys	irix_unimp	0	/* 1209  nothin'	       V*/
	sys	irix_unimp	0	/* 1210  XXX pidsprocsp()	*/
	sys	irix_unimp	0	/* 1211  XXX rexec()		*/
	sys	irix_unimp	0	/* 1212  XXX timer_create()	*/
	sys	irix_unimp	0	/* 1213  XXX timer_delete()	*/
	sys	irix_unimp	0	/* 1214  XXX timer_settime()	*/
	sys	irix_unimp	0	/* 1215  XXX timer_gettime()	*/
	sys	irix_unimp	0	/* 1216  XXX timer_setoverrun()	*/
	sys	sys_sched_rr_get_interval 2		/* 1217  sched_rr_get_interval()V*/
	sys	sys_sched_yield	0	/* 1218  sched_yield()	       V*/
	sys	sys_sched_getscheduler 1 /* 1219  sched_getscheduler()  V*/
	sys	sys_sched_setscheduler 3 /* 1220  sched_setscheduler()  V*/
	sys	sys_sched_getparam 2	/* 1221  sched_getparam()      V*/
	sys	sys_sched_setparam 2	/* 1222  sched_setparam()      V*/
	sys	irix_unimp	0	/* 1223  XXX usync_cntl()	*/
	sys	irix_unimp	0	/* 1224  XXX psema_cntl()	*/
	sys	irix_unimp	0	/* 1225  XXX restartreturn()	*/

	/* Just to pad things out nicely. */
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0
	sys	irix_unimp	0

	.endm

	/*
	 * Pre-compute the number of _instruction_ bytes needed to load
	 * or store the arguments 6-8. Negative values are ignored.
	 */
	.macro  sys function, nargs
	PTR	\function
	LONG	(\nargs << 2) - (5 << 2)
	.endm

	.align	4
EXPORT(sys_call_table_irix5)
	irix5syscalltable
