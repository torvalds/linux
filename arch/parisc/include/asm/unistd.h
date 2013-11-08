#ifndef _ASM_PARISC_UNISTD_H_
#define _ASM_PARISC_UNISTD_H_

/*
 * This file contains the system call numbers.
 */

/*
 *   HP-UX system calls get their native numbers for binary compatibility.
 */

#define __NR_HPUX_exit                    1
#define __NR_HPUX_fork                    2
#define __NR_HPUX_read                    3
#define __NR_HPUX_write                   4
#define __NR_HPUX_open                    5
#define __NR_HPUX_close                   6
#define __NR_HPUX_wait                    7
#define __NR_HPUX_creat                   8
#define __NR_HPUX_link                    9
#define __NR_HPUX_unlink                 10
#define __NR_HPUX_execv                  11
#define __NR_HPUX_chdir                  12
#define __NR_HPUX_time                   13
#define __NR_HPUX_mknod                  14
#define __NR_HPUX_chmod                  15
#define __NR_HPUX_chown                  16
#define __NR_HPUX_break                  17
#define __NR_HPUX_lchmod                 18
#define __NR_HPUX_lseek                  19
#define __NR_HPUX_getpid                 20
#define __NR_HPUX_mount                  21
#define __NR_HPUX_umount                 22
#define __NR_HPUX_setuid                 23
#define __NR_HPUX_getuid                 24
#define __NR_HPUX_stime                  25
#define __NR_HPUX_ptrace                 26
#define __NR_HPUX_alarm                  27
#define __NR_HPUX_oldfstat               28
#define __NR_HPUX_pause                  29
#define __NR_HPUX_utime                  30
#define __NR_HPUX_stty                   31
#define __NR_HPUX_gtty                   32
#define __NR_HPUX_access                 33
#define __NR_HPUX_nice                   34
#define __NR_HPUX_ftime                  35
#define __NR_HPUX_sync                   36
#define __NR_HPUX_kill                   37
#define __NR_HPUX_stat                   38
#define __NR_HPUX_setpgrp3               39
#define __NR_HPUX_lstat                  40
#define __NR_HPUX_dup                    41
#define __NR_HPUX_pipe                   42
#define __NR_HPUX_times                  43
#define __NR_HPUX_profil                 44
#define __NR_HPUX_ki_call                45
#define __NR_HPUX_setgid                 46
#define __NR_HPUX_getgid                 47
#define __NR_HPUX_sigsys                 48
#define __NR_HPUX_reserved1              49
#define __NR_HPUX_reserved2              50
#define __NR_HPUX_acct                   51
#define __NR_HPUX_set_userthreadid       52
#define __NR_HPUX_oldlock                53
#define __NR_HPUX_ioctl                  54
#define __NR_HPUX_reboot                 55
#define __NR_HPUX_symlink                56
#define __NR_HPUX_utssys                 57
#define __NR_HPUX_readlink               58
#define __NR_HPUX_execve                 59
#define __NR_HPUX_umask                  60
#define __NR_HPUX_chroot                 61
#define __NR_HPUX_fcntl                  62
#define __NR_HPUX_ulimit                 63
#define __NR_HPUX_getpagesize            64
#define __NR_HPUX_mremap                 65
#define __NR_HPUX_vfork                  66
#define __NR_HPUX_vread                  67
#define __NR_HPUX_vwrite                 68
#define __NR_HPUX_sbrk                   69
#define __NR_HPUX_sstk                   70
#define __NR_HPUX_mmap                   71
#define __NR_HPUX_vadvise                72
#define __NR_HPUX_munmap                 73
#define __NR_HPUX_mprotect               74
#define __NR_HPUX_madvise                75
#define __NR_HPUX_vhangup                76
#define __NR_HPUX_swapoff                77
#define __NR_HPUX_mincore                78
#define __NR_HPUX_getgroups              79
#define __NR_HPUX_setgroups              80
#define __NR_HPUX_getpgrp2               81
#define __NR_HPUX_setpgrp2               82
#define __NR_HPUX_setitimer              83
#define __NR_HPUX_wait3                  84
#define __NR_HPUX_swapon                 85
#define __NR_HPUX_getitimer              86
#define __NR_HPUX_gethostname42          87
#define __NR_HPUX_sethostname42          88
#define __NR_HPUX_getdtablesize          89
#define __NR_HPUX_dup2                   90
#define __NR_HPUX_getdopt                91
#define __NR_HPUX_fstat                  92
#define __NR_HPUX_select                 93
#define __NR_HPUX_setdopt                94
#define __NR_HPUX_fsync                  95
#define __NR_HPUX_setpriority            96
#define __NR_HPUX_socket_old             97
#define __NR_HPUX_connect_old            98
#define __NR_HPUX_accept_old             99
#define __NR_HPUX_getpriority           100
#define __NR_HPUX_send_old              101
#define __NR_HPUX_recv_old              102
#define __NR_HPUX_socketaddr_old        103
#define __NR_HPUX_bind_old              104
#define __NR_HPUX_setsockopt_old        105
#define __NR_HPUX_listen_old            106
#define __NR_HPUX_vtimes_old            107
#define __NR_HPUX_sigvector             108
#define __NR_HPUX_sigblock              109
#define __NR_HPUX_siggetmask            110
#define __NR_HPUX_sigpause              111
#define __NR_HPUX_sigstack              112
#define __NR_HPUX_recvmsg_old           113
#define __NR_HPUX_sendmsg_old           114
#define __NR_HPUX_vtrace_old            115
#define __NR_HPUX_gettimeofday          116
#define __NR_HPUX_getrusage             117
#define __NR_HPUX_getsockopt_old        118
#define __NR_HPUX_resuba_old            119
#define __NR_HPUX_readv                 120
#define __NR_HPUX_writev                121
#define __NR_HPUX_settimeofday          122
#define __NR_HPUX_fchown                123
#define __NR_HPUX_fchmod                124
#define __NR_HPUX_recvfrom_old          125
#define __NR_HPUX_setresuid             126
#define __NR_HPUX_setresgid             127
#define __NR_HPUX_rename                128
#define __NR_HPUX_truncate              129
#define __NR_HPUX_ftruncate             130
#define __NR_HPUX_flock_old             131
#define __NR_HPUX_sysconf               132
#define __NR_HPUX_sendto_old            133
#define __NR_HPUX_shutdown_old          134
#define __NR_HPUX_socketpair_old        135
#define __NR_HPUX_mkdir                 136
#define __NR_HPUX_rmdir                 137
#define __NR_HPUX_utimes_old            138
#define __NR_HPUX_sigcleanup_old        139
#define __NR_HPUX_setcore               140
#define __NR_HPUX_getpeername_old       141
#define __NR_HPUX_gethostid             142
#define __NR_HPUX_sethostid             143
#define __NR_HPUX_getrlimit             144
#define __NR_HPUX_setrlimit             145
#define __NR_HPUX_killpg_old            146
#define __NR_HPUX_cachectl              147
#define __NR_HPUX_quotactl              148
#define __NR_HPUX_get_sysinfo           149
#define __NR_HPUX_getsockname_old       150
#define __NR_HPUX_privgrp               151
#define __NR_HPUX_rtprio                152
#define __NR_HPUX_plock                 153
#define __NR_HPUX_reserved3             154
#define __NR_HPUX_lockf                 155
#define __NR_HPUX_semget                156
#define __NR_HPUX_osemctl               157
#define __NR_HPUX_semop                 158
#define __NR_HPUX_msgget                159
#define __NR_HPUX_omsgctl               160
#define __NR_HPUX_msgsnd                161
#define __NR_HPUX_msgrecv               162
#define __NR_HPUX_shmget                163
#define __NR_HPUX_oshmctl               164
#define __NR_HPUX_shmat                 165
#define __NR_HPUX_shmdt                 166
#define __NR_HPUX_m68020_advise         167
/* [168,189] are for Discless/DUX */
#define __NR_HPUX_csp                   168
#define __NR_HPUX_cluster               169
#define __NR_HPUX_mkrnod                170
#define __NR_HPUX_test                  171
#define __NR_HPUX_unsp_open             172
#define __NR_HPUX_reserved4             173
#define __NR_HPUX_getcontext_old        174
#define __NR_HPUX_osetcontext           175
#define __NR_HPUX_bigio                 176
#define __NR_HPUX_pipenode              177
#define __NR_HPUX_lsync                 178
#define __NR_HPUX_getmachineid          179
#define __NR_HPUX_cnodeid               180
#define __NR_HPUX_cnodes                181
#define __NR_HPUX_swapclients           182
#define __NR_HPUX_rmt_process           183
#define __NR_HPUX_dskless_stats         184
#define __NR_HPUX_sigprocmask           185
#define __NR_HPUX_sigpending            186
#define __NR_HPUX_sigsuspend            187
#define __NR_HPUX_sigaction             188
#define __NR_HPUX_reserved5             189
#define __NR_HPUX_nfssvc                190
#define __NR_HPUX_getfh                 191
#define __NR_HPUX_getdomainname         192
#define __NR_HPUX_setdomainname         193
#define __NR_HPUX_async_daemon          194
#define __NR_HPUX_getdirentries         195
#define __NR_HPUX_statfs                196
#define __NR_HPUX_fstatfs               197
#define __NR_HPUX_vfsmount              198
#define __NR_HPUX_reserved6             199
#define __NR_HPUX_waitpid               200
/* 201 - 223 missing */
#define __NR_HPUX_sigsetreturn          224
#define __NR_HPUX_sigsetstatemask       225
/* 226 missing */
#define __NR_HPUX_cs                    227
#define __NR_HPUX_cds                   228
#define __NR_HPUX_set_no_trunc          229
#define __NR_HPUX_pathconf              230
#define __NR_HPUX_fpathconf             231
/* 232, 233 missing */
#define __NR_HPUX_nfs_fcntl             234
#define __NR_HPUX_ogetacl               235
#define __NR_HPUX_ofgetacl              236
#define __NR_HPUX_osetacl               237
#define __NR_HPUX_ofsetacl              238
#define __NR_HPUX_pstat                 239
#define __NR_HPUX_getaudid              240
#define __NR_HPUX_setaudid              241
#define __NR_HPUX_getaudproc            242
#define __NR_HPUX_setaudproc            243
#define __NR_HPUX_getevent              244
#define __NR_HPUX_setevent              245
#define __NR_HPUX_audwrite              246
#define __NR_HPUX_audswitch             247
#define __NR_HPUX_audctl                248
#define __NR_HPUX_ogetaccess            249
#define __NR_HPUX_fsctl                 250
/* 251 - 258 missing */
#define __NR_HPUX_swapfs                259
#define __NR_HPUX_fss                   260
/* 261 - 266 missing */
#define __NR_HPUX_tsync                 267
#define __NR_HPUX_getnumfds             268
#define __NR_HPUX_poll                  269
#define __NR_HPUX_getmsg                270
#define __NR_HPUX_putmsg                271
#define __NR_HPUX_fchdir                272
#define __NR_HPUX_getmount_cnt          273
#define __NR_HPUX_getmount_entry        274
#define __NR_HPUX_accept                275
#define __NR_HPUX_bind                  276
#define __NR_HPUX_connect               277
#define __NR_HPUX_getpeername           278
#define __NR_HPUX_getsockname           279
#define __NR_HPUX_getsockopt            280
#define __NR_HPUX_listen                281
#define __NR_HPUX_recv                  282
#define __NR_HPUX_recvfrom              283
#define __NR_HPUX_recvmsg               284
#define __NR_HPUX_send                  285
#define __NR_HPUX_sendmsg               286
#define __NR_HPUX_sendto                287
#define __NR_HPUX_setsockopt            288
#define __NR_HPUX_shutdown              289
#define __NR_HPUX_socket                290
#define __NR_HPUX_socketpair            291
#define __NR_HPUX_proc_open             292
#define __NR_HPUX_proc_close            293
#define __NR_HPUX_proc_send             294
#define __NR_HPUX_proc_recv             295
#define __NR_HPUX_proc_sendrecv         296
#define __NR_HPUX_proc_syscall          297
/* 298 - 311 missing */
#define __NR_HPUX_semctl                312
#define __NR_HPUX_msgctl                313
#define __NR_HPUX_shmctl                314
#define __NR_HPUX_mpctl                 315
#define __NR_HPUX_exportfs              316
#define __NR_HPUX_getpmsg               317
#define __NR_HPUX_putpmsg               318
/* 319 missing */
#define __NR_HPUX_msync                 320
#define __NR_HPUX_msleep                321
#define __NR_HPUX_mwakeup               322
#define __NR_HPUX_msem_init             323
#define __NR_HPUX_msem_remove           324
#define __NR_HPUX_adjtime               325
#define __NR_HPUX_kload                 326
#define __NR_HPUX_fattach               327
#define __NR_HPUX_fdetach               328
#define __NR_HPUX_serialize             329
#define __NR_HPUX_statvfs               330
#define __NR_HPUX_fstatvfs              331
#define __NR_HPUX_lchown                332
#define __NR_HPUX_getsid                333
#define __NR_HPUX_sysfs                 334
/* 335, 336 missing */
#define __NR_HPUX_sched_setparam        337
#define __NR_HPUX_sched_getparam        338
#define __NR_HPUX_sched_setscheduler    339
#define __NR_HPUX_sched_getscheduler    340
#define __NR_HPUX_sched_yield           341
#define __NR_HPUX_sched_get_priority_max 342
#define __NR_HPUX_sched_get_priority_min 343
#define __NR_HPUX_sched_rr_get_interval 344
#define __NR_HPUX_clock_settime         345
#define __NR_HPUX_clock_gettime         346
#define __NR_HPUX_clock_getres          347
#define __NR_HPUX_timer_create          348
#define __NR_HPUX_timer_delete          349
#define __NR_HPUX_timer_settime         350
#define __NR_HPUX_timer_gettime         351
#define __NR_HPUX_timer_getoverrun      352
#define __NR_HPUX_nanosleep             353
#define __NR_HPUX_toolbox               354
/* 355 missing */
#define __NR_HPUX_getdents              356
#define __NR_HPUX_getcontext            357
#define __NR_HPUX_sysinfo               358
#define __NR_HPUX_fcntl64               359
#define __NR_HPUX_ftruncate64           360
#define __NR_HPUX_fstat64               361
#define __NR_HPUX_getdirentries64       362
#define __NR_HPUX_getrlimit64           363
#define __NR_HPUX_lockf64               364
#define __NR_HPUX_lseek64               365
#define __NR_HPUX_lstat64               366
#define __NR_HPUX_mmap64                367
#define __NR_HPUX_setrlimit64           368
#define __NR_HPUX_stat64                369
#define __NR_HPUX_truncate64            370
#define __NR_HPUX_ulimit64              371
#define __NR_HPUX_pread                 372
#define __NR_HPUX_preadv                373
#define __NR_HPUX_pwrite                374
#define __NR_HPUX_pwritev               375
#define __NR_HPUX_pread64               376
#define __NR_HPUX_preadv64              377
#define __NR_HPUX_pwrite64              378
#define __NR_HPUX_pwritev64             379
#define __NR_HPUX_setcontext            380
#define __NR_HPUX_sigaltstack           381
#define __NR_HPUX_waitid                382
#define __NR_HPUX_setpgrp               383
#define __NR_HPUX_recvmsg2              384
#define __NR_HPUX_sendmsg2              385
#define __NR_HPUX_socket2               386
#define __NR_HPUX_socketpair2           387
#define __NR_HPUX_setregid              388
#define __NR_HPUX_lwp_create            389
#define __NR_HPUX_lwp_terminate         390
#define __NR_HPUX_lwp_wait              391
#define __NR_HPUX_lwp_suspend           392
#define __NR_HPUX_lwp_resume            393
/* 394 missing */
#define __NR_HPUX_lwp_abort_syscall     395
#define __NR_HPUX_lwp_info              396
#define __NR_HPUX_lwp_kill              397
#define __NR_HPUX_ksleep                398
#define __NR_HPUX_kwakeup               399
/* 400 missing */
#define __NR_HPUX_pstat_getlwp          401
#define __NR_HPUX_lwp_exit              402
#define __NR_HPUX_lwp_continue          403
#define __NR_HPUX_getacl                404
#define __NR_HPUX_fgetacl               405
#define __NR_HPUX_setacl                406
#define __NR_HPUX_fsetacl               407
#define __NR_HPUX_getaccess             408
#define __NR_HPUX_lwp_mutex_init        409
#define __NR_HPUX_lwp_mutex_lock_sys    410
#define __NR_HPUX_lwp_mutex_unlock      411
#define __NR_HPUX_lwp_cond_init         412
#define __NR_HPUX_lwp_cond_signal       413
#define __NR_HPUX_lwp_cond_broadcast    414
#define __NR_HPUX_lwp_cond_wait_sys     415
#define __NR_HPUX_lwp_getscheduler      416
#define __NR_HPUX_lwp_setscheduler      417
#define __NR_HPUX_lwp_getstate          418
#define __NR_HPUX_lwp_setstate          419
#define __NR_HPUX_lwp_detach            420
#define __NR_HPUX_mlock                 421
#define __NR_HPUX_munlock               422
#define __NR_HPUX_mlockall              423
#define __NR_HPUX_munlockall            424
#define __NR_HPUX_shm_open              425
#define __NR_HPUX_shm_unlink            426
#define __NR_HPUX_sigqueue              427
#define __NR_HPUX_sigwaitinfo           428
#define __NR_HPUX_sigtimedwait          429
#define __NR_HPUX_sigwait               430
#define __NR_HPUX_aio_read              431
#define __NR_HPUX_aio_write             432
#define __NR_HPUX_lio_listio            433
#define __NR_HPUX_aio_error             434
#define __NR_HPUX_aio_return            435
#define __NR_HPUX_aio_cancel            436
#define __NR_HPUX_aio_suspend           437
#define __NR_HPUX_aio_fsync             438
#define __NR_HPUX_mq_open               439
#define __NR_HPUX_mq_close              440
#define __NR_HPUX_mq_unlink             441
#define __NR_HPUX_mq_send               442
#define __NR_HPUX_mq_receive            443
#define __NR_HPUX_mq_notify             444
#define __NR_HPUX_mq_setattr            445
#define __NR_HPUX_mq_getattr            446
#define __NR_HPUX_ksem_open             447
#define __NR_HPUX_ksem_unlink           448
#define __NR_HPUX_ksem_close            449
#define __NR_HPUX_ksem_post             450
#define __NR_HPUX_ksem_wait             451
#define __NR_HPUX_ksem_read             452
#define __NR_HPUX_ksem_trywait          453
#define __NR_HPUX_lwp_rwlock_init       454
#define __NR_HPUX_lwp_rwlock_destroy    455
#define __NR_HPUX_lwp_rwlock_rdlock_sys 456
#define __NR_HPUX_lwp_rwlock_wrlock_sys 457
#define __NR_HPUX_lwp_rwlock_tryrdlock  458
#define __NR_HPUX_lwp_rwlock_trywrlock  459
#define __NR_HPUX_lwp_rwlock_unlock     460
#define __NR_HPUX_ttrace                461
#define __NR_HPUX_ttrace_wait           462
#define __NR_HPUX_lf_wire_mem           463
#define __NR_HPUX_lf_unwire_mem         464
#define __NR_HPUX_lf_send_pin_map       465
#define __NR_HPUX_lf_free_buf           466
#define __NR_HPUX_lf_wait_nq            467
#define __NR_HPUX_lf_wakeup_conn_q      468
#define __NR_HPUX_lf_unused             469
#define __NR_HPUX_lwp_sema_init         470
#define __NR_HPUX_lwp_sema_post         471
#define __NR_HPUX_lwp_sema_wait         472
#define __NR_HPUX_lwp_sema_trywait      473
#define __NR_HPUX_lwp_sema_destroy      474
#define __NR_HPUX_statvfs64             475
#define __NR_HPUX_fstatvfs64            476
#define __NR_HPUX_msh_register          477
#define __NR_HPUX_ptrace64              478
#define __NR_HPUX_sendfile              479
#define __NR_HPUX_sendpath              480
#define __NR_HPUX_sendfile64            481
#define __NR_HPUX_sendpath64            482
#define __NR_HPUX_modload               483
#define __NR_HPUX_moduload              484
#define __NR_HPUX_modpath               485
#define __NR_HPUX_getksym               486
#define __NR_HPUX_modadm                487
#define __NR_HPUX_modstat               488
#define __NR_HPUX_lwp_detached_exit     489
#define __NR_HPUX_crashconf             490
#define __NR_HPUX_siginhibit            491
#define __NR_HPUX_sigenable             492
#define __NR_HPUX_spuctl                493
#define __NR_HPUX_zerokernelsum         494
#define __NR_HPUX_nfs_kstat             495
#define __NR_HPUX_aio_read64            496
#define __NR_HPUX_aio_write64           497
#define __NR_HPUX_aio_error64           498
#define __NR_HPUX_aio_return64          499
#define __NR_HPUX_aio_cancel64          500
#define __NR_HPUX_aio_suspend64         501
#define __NR_HPUX_aio_fsync64           502
#define __NR_HPUX_lio_listio64          503
#define __NR_HPUX_recv2                 504
#define __NR_HPUX_recvfrom2             505
#define __NR_HPUX_send2                 506
#define __NR_HPUX_sendto2               507
#define __NR_HPUX_acl                   508
#define __NR_HPUX___cnx_p2p_ctl         509
#define __NR_HPUX___cnx_gsched_ctl      510
#define __NR_HPUX___cnx_pmon_ctl        511

#define __NR_HPUX_syscalls		512

/*
 * Linux system call numbers.
 *
 * Cary Coutant says that we should just use another syscall gateway
 * page to avoid clashing with the HPUX space, and I think he's right:
 * it will would keep a branch out of our syscall entry path, at the
 * very least.  If we decide to change it later, we can ``just'' tweak
 * the LINUX_GATEWAY_ADDR define at the bottom and make __NR_Linux be
 * 1024 or something.  Oh, and recompile libc. =)
 *
 * 64-bit HPUX binaries get the syscall gateway address passed in a register
 * from the kernel at startup, which seems a sane strategy.
 */

#define __NR_Linux                0
#define __NR_restart_syscall      (__NR_Linux + 0)
#define __NR_exit                 (__NR_Linux + 1)
#define __NR_fork                 (__NR_Linux + 2)
#define __NR_read                 (__NR_Linux + 3)
#define __NR_write                (__NR_Linux + 4)
#define __NR_open                 (__NR_Linux + 5)
#define __NR_close                (__NR_Linux + 6)
#define __NR_waitpid              (__NR_Linux + 7)
#define __NR_creat                (__NR_Linux + 8)
#define __NR_link                 (__NR_Linux + 9)
#define __NR_unlink              (__NR_Linux + 10)
#define __NR_execve              (__NR_Linux + 11)
#define __NR_chdir               (__NR_Linux + 12)
#define __NR_time                (__NR_Linux + 13)
#define __NR_mknod               (__NR_Linux + 14)
#define __NR_chmod               (__NR_Linux + 15)
#define __NR_lchown              (__NR_Linux + 16)
#define __NR_socket              (__NR_Linux + 17)
#define __NR_stat                (__NR_Linux + 18)
#define __NR_lseek               (__NR_Linux + 19)
#define __NR_getpid              (__NR_Linux + 20)
#define __NR_mount               (__NR_Linux + 21)
#define __NR_bind                (__NR_Linux + 22)
#define __NR_setuid              (__NR_Linux + 23)
#define __NR_getuid              (__NR_Linux + 24)
#define __NR_stime               (__NR_Linux + 25)
#define __NR_ptrace              (__NR_Linux + 26)
#define __NR_alarm               (__NR_Linux + 27)
#define __NR_fstat               (__NR_Linux + 28)
#define __NR_pause               (__NR_Linux + 29)
#define __NR_utime               (__NR_Linux + 30)
#define __NR_connect             (__NR_Linux + 31)
#define __NR_listen              (__NR_Linux + 32)
#define __NR_access              (__NR_Linux + 33)
#define __NR_nice                (__NR_Linux + 34)
#define __NR_accept              (__NR_Linux + 35)
#define __NR_sync                (__NR_Linux + 36)
#define __NR_kill                (__NR_Linux + 37)
#define __NR_rename              (__NR_Linux + 38)
#define __NR_mkdir               (__NR_Linux + 39)
#define __NR_rmdir               (__NR_Linux + 40)
#define __NR_dup                 (__NR_Linux + 41)
#define __NR_pipe                (__NR_Linux + 42)
#define __NR_times               (__NR_Linux + 43)
#define __NR_getsockname         (__NR_Linux + 44)
#define __NR_brk                 (__NR_Linux + 45)
#define __NR_setgid              (__NR_Linux + 46)
#define __NR_getgid              (__NR_Linux + 47)
#define __NR_signal              (__NR_Linux + 48)
#define __NR_geteuid             (__NR_Linux + 49)
#define __NR_getegid             (__NR_Linux + 50)
#define __NR_acct                (__NR_Linux + 51)
#define __NR_umount2             (__NR_Linux + 52)
#define __NR_getpeername         (__NR_Linux + 53)
#define __NR_ioctl               (__NR_Linux + 54)
#define __NR_fcntl               (__NR_Linux + 55)
#define __NR_socketpair          (__NR_Linux + 56)
#define __NR_setpgid             (__NR_Linux + 57)
#define __NR_send                (__NR_Linux + 58)
#define __NR_uname               (__NR_Linux + 59)
#define __NR_umask               (__NR_Linux + 60)
#define __NR_chroot              (__NR_Linux + 61)
#define __NR_ustat               (__NR_Linux + 62)
#define __NR_dup2                (__NR_Linux + 63)
#define __NR_getppid             (__NR_Linux + 64)
#define __NR_getpgrp             (__NR_Linux + 65)
#define __NR_setsid              (__NR_Linux + 66)
#define __NR_pivot_root          (__NR_Linux + 67)
#define __NR_sgetmask            (__NR_Linux + 68)
#define __NR_ssetmask            (__NR_Linux + 69)
#define __NR_setreuid            (__NR_Linux + 70)
#define __NR_setregid            (__NR_Linux + 71)
#define __NR_mincore             (__NR_Linux + 72)
#define __NR_sigpending          (__NR_Linux + 73)
#define __NR_sethostname         (__NR_Linux + 74)
#define __NR_setrlimit           (__NR_Linux + 75)
#define __NR_getrlimit           (__NR_Linux + 76)
#define __NR_getrusage           (__NR_Linux + 77)
#define __NR_gettimeofday        (__NR_Linux + 78)
#define __NR_settimeofday        (__NR_Linux + 79)
#define __NR_getgroups           (__NR_Linux + 80)
#define __NR_setgroups           (__NR_Linux + 81)
#define __NR_sendto              (__NR_Linux + 82)
#define __NR_symlink             (__NR_Linux + 83)
#define __NR_lstat               (__NR_Linux + 84)
#define __NR_readlink            (__NR_Linux + 85)
#define __NR_uselib              (__NR_Linux + 86)
#define __NR_swapon              (__NR_Linux + 87)
#define __NR_reboot              (__NR_Linux + 88)
#define __NR_mmap2             (__NR_Linux + 89)
#define __NR_mmap                (__NR_Linux + 90)
#define __NR_munmap              (__NR_Linux + 91)
#define __NR_truncate            (__NR_Linux + 92)
#define __NR_ftruncate           (__NR_Linux + 93)
#define __NR_fchmod              (__NR_Linux + 94)
#define __NR_fchown              (__NR_Linux + 95)
#define __NR_getpriority         (__NR_Linux + 96)
#define __NR_setpriority         (__NR_Linux + 97)
#define __NR_recv                (__NR_Linux + 98)
#define __NR_statfs              (__NR_Linux + 99)
#define __NR_fstatfs            (__NR_Linux + 100)
#define __NR_stat64           (__NR_Linux + 101)
/* #define __NR_socketcall         (__NR_Linux + 102) */
#define __NR_syslog             (__NR_Linux + 103)
#define __NR_setitimer          (__NR_Linux + 104)
#define __NR_getitimer          (__NR_Linux + 105)
#define __NR_capget             (__NR_Linux + 106)
#define __NR_capset             (__NR_Linux + 107)
#define __NR_pread64            (__NR_Linux + 108)
#define __NR_pwrite64           (__NR_Linux + 109)
#define __NR_getcwd             (__NR_Linux + 110)
#define __NR_vhangup            (__NR_Linux + 111)
#define __NR_fstat64            (__NR_Linux + 112)
#define __NR_vfork              (__NR_Linux + 113)
#define __NR_wait4              (__NR_Linux + 114)
#define __NR_swapoff            (__NR_Linux + 115)
#define __NR_sysinfo            (__NR_Linux + 116)
#define __NR_shutdown           (__NR_Linux + 117)
#define __NR_fsync              (__NR_Linux + 118)
#define __NR_madvise            (__NR_Linux + 119)
#define __NR_clone              (__NR_Linux + 120)
#define __NR_setdomainname      (__NR_Linux + 121)
#define __NR_sendfile           (__NR_Linux + 122)
#define __NR_recvfrom           (__NR_Linux + 123)
#define __NR_adjtimex           (__NR_Linux + 124)
#define __NR_mprotect           (__NR_Linux + 125)
#define __NR_sigprocmask        (__NR_Linux + 126)
#define __NR_create_module      (__NR_Linux + 127)
#define __NR_init_module        (__NR_Linux + 128)
#define __NR_delete_module      (__NR_Linux + 129)
#define __NR_get_kernel_syms    (__NR_Linux + 130)
#define __NR_quotactl           (__NR_Linux + 131)
#define __NR_getpgid            (__NR_Linux + 132)
#define __NR_fchdir             (__NR_Linux + 133)
#define __NR_bdflush            (__NR_Linux + 134)
#define __NR_sysfs              (__NR_Linux + 135)
#define __NR_personality        (__NR_Linux + 136)
#define __NR_afs_syscall        (__NR_Linux + 137) /* Syscall for Andrew File System */
#define __NR_setfsuid           (__NR_Linux + 138)
#define __NR_setfsgid           (__NR_Linux + 139)
#define __NR__llseek            (__NR_Linux + 140)
#define __NR_getdents           (__NR_Linux + 141)
#define __NR__newselect         (__NR_Linux + 142)
#define __NR_flock              (__NR_Linux + 143)
#define __NR_msync              (__NR_Linux + 144)
#define __NR_readv              (__NR_Linux + 145)
#define __NR_writev             (__NR_Linux + 146)
#define __NR_getsid             (__NR_Linux + 147)
#define __NR_fdatasync          (__NR_Linux + 148)
#define __NR__sysctl            (__NR_Linux + 149)
#define __NR_mlock              (__NR_Linux + 150)
#define __NR_munlock            (__NR_Linux + 151)
#define __NR_mlockall           (__NR_Linux + 152)
#define __NR_munlockall         (__NR_Linux + 153)
#define __NR_sched_setparam             (__NR_Linux + 154)
#define __NR_sched_getparam             (__NR_Linux + 155)
#define __NR_sched_setscheduler         (__NR_Linux + 156)
#define __NR_sched_getscheduler         (__NR_Linux + 157)
#define __NR_sched_yield                (__NR_Linux + 158)
#define __NR_sched_get_priority_max     (__NR_Linux + 159)
#define __NR_sched_get_priority_min     (__NR_Linux + 160)
#define __NR_sched_rr_get_interval      (__NR_Linux + 161)
#define __NR_nanosleep          (__NR_Linux + 162)
#define __NR_mremap             (__NR_Linux + 163)
#define __NR_setresuid          (__NR_Linux + 164)
#define __NR_getresuid          (__NR_Linux + 165)
#define __NR_sigaltstack        (__NR_Linux + 166)
#define __NR_query_module       (__NR_Linux + 167)
#define __NR_poll               (__NR_Linux + 168)
#define __NR_nfsservctl         (__NR_Linux + 169)
#define __NR_setresgid          (__NR_Linux + 170)
#define __NR_getresgid          (__NR_Linux + 171)
#define __NR_prctl              (__NR_Linux + 172)
#define __NR_rt_sigreturn       (__NR_Linux + 173)
#define __NR_rt_sigaction       (__NR_Linux + 174)
#define __NR_rt_sigprocmask     (__NR_Linux + 175)
#define __NR_rt_sigpending      (__NR_Linux + 176)
#define __NR_rt_sigtimedwait    (__NR_Linux + 177)
#define __NR_rt_sigqueueinfo    (__NR_Linux + 178)
#define __NR_rt_sigsuspend      (__NR_Linux + 179)
#define __NR_chown              (__NR_Linux + 180)
#define __NR_setsockopt         (__NR_Linux + 181)
#define __NR_getsockopt         (__NR_Linux + 182)
#define __NR_sendmsg            (__NR_Linux + 183)
#define __NR_recvmsg            (__NR_Linux + 184)
#define __NR_semop              (__NR_Linux + 185)
#define __NR_semget             (__NR_Linux + 186)
#define __NR_semctl             (__NR_Linux + 187)
#define __NR_msgsnd             (__NR_Linux + 188)
#define __NR_msgrcv             (__NR_Linux + 189)
#define __NR_msgget             (__NR_Linux + 190)
#define __NR_msgctl             (__NR_Linux + 191)
#define __NR_shmat              (__NR_Linux + 192)
#define __NR_shmdt              (__NR_Linux + 193)
#define __NR_shmget             (__NR_Linux + 194)
#define __NR_shmctl             (__NR_Linux + 195)

#define __NR_getpmsg		(__NR_Linux + 196) /* Somebody *wants* streams? */
#define __NR_putpmsg		(__NR_Linux + 197)

#define __NR_lstat64            (__NR_Linux + 198)
#define __NR_truncate64         (__NR_Linux + 199)
#define __NR_ftruncate64        (__NR_Linux + 200)
#define __NR_getdents64         (__NR_Linux + 201)
#define __NR_fcntl64            (__NR_Linux + 202)
#define __NR_attrctl            (__NR_Linux + 203)
#define __NR_acl_get            (__NR_Linux + 204)
#define __NR_acl_set            (__NR_Linux + 205)
#define __NR_gettid             (__NR_Linux + 206)
#define __NR_readahead          (__NR_Linux + 207)
#define __NR_tkill              (__NR_Linux + 208)
#define __NR_sendfile64         (__NR_Linux + 209)
#define __NR_futex              (__NR_Linux + 210)
#define __NR_sched_setaffinity  (__NR_Linux + 211)
#define __NR_sched_getaffinity  (__NR_Linux + 212)
#define __NR_set_thread_area    (__NR_Linux + 213)
#define __NR_get_thread_area    (__NR_Linux + 214)
#define __NR_io_setup           (__NR_Linux + 215)
#define __NR_io_destroy         (__NR_Linux + 216)
#define __NR_io_getevents       (__NR_Linux + 217)
#define __NR_io_submit          (__NR_Linux + 218)
#define __NR_io_cancel          (__NR_Linux + 219)
#define __NR_alloc_hugepages    (__NR_Linux + 220)
#define __NR_free_hugepages     (__NR_Linux + 221)
#define __NR_exit_group         (__NR_Linux + 222)
#define __NR_lookup_dcookie     (__NR_Linux + 223)
#define __NR_epoll_create       (__NR_Linux + 224)
#define __NR_epoll_ctl          (__NR_Linux + 225)
#define __NR_epoll_wait         (__NR_Linux + 226)
#define __NR_remap_file_pages   (__NR_Linux + 227)
#define __NR_semtimedop         (__NR_Linux + 228)
#define __NR_mq_open            (__NR_Linux + 229)
#define __NR_mq_unlink          (__NR_Linux + 230)
#define __NR_mq_timedsend       (__NR_Linux + 231)
#define __NR_mq_timedreceive    (__NR_Linux + 232)
#define __NR_mq_notify          (__NR_Linux + 233)
#define __NR_mq_getsetattr      (__NR_Linux + 234)
#define __NR_waitid		(__NR_Linux + 235)
#define __NR_fadvise64_64	(__NR_Linux + 236)
#define __NR_set_tid_address	(__NR_Linux + 237)
#define __NR_setxattr		(__NR_Linux + 238)
#define __NR_lsetxattr		(__NR_Linux + 239)
#define __NR_fsetxattr		(__NR_Linux + 240)
#define __NR_getxattr		(__NR_Linux + 241)
#define __NR_lgetxattr		(__NR_Linux + 242)
#define __NR_fgetxattr		(__NR_Linux + 243)
#define __NR_listxattr		(__NR_Linux + 244)
#define __NR_llistxattr		(__NR_Linux + 245)
#define __NR_flistxattr		(__NR_Linux + 246)
#define __NR_removexattr	(__NR_Linux + 247)
#define __NR_lremovexattr	(__NR_Linux + 248)
#define __NR_fremovexattr	(__NR_Linux + 249)
#define __NR_timer_create	(__NR_Linux + 250)
#define __NR_timer_settime	(__NR_Linux + 251)
#define __NR_timer_gettime	(__NR_Linux + 252)
#define __NR_timer_getoverrun	(__NR_Linux + 253)
#define __NR_timer_delete	(__NR_Linux + 254)
#define __NR_clock_settime	(__NR_Linux + 255)
#define __NR_clock_gettime	(__NR_Linux + 256)
#define __NR_clock_getres	(__NR_Linux + 257)
#define __NR_clock_nanosleep	(__NR_Linux + 258)
#define __NR_tgkill		(__NR_Linux + 259)
#define __NR_mbind		(__NR_Linux + 260)
#define __NR_get_mempolicy	(__NR_Linux + 261)
#define __NR_set_mempolicy	(__NR_Linux + 262)
#define __NR_vserver		(__NR_Linux + 263)
#define __NR_add_key		(__NR_Linux + 264)
#define __NR_request_key	(__NR_Linux + 265)
#define __NR_keyctl		(__NR_Linux + 266)
#define __NR_ioprio_set		(__NR_Linux + 267)
#define __NR_ioprio_get		(__NR_Linux + 268)
#define __NR_inotify_init	(__NR_Linux + 269)
#define __NR_inotify_add_watch	(__NR_Linux + 270)
#define __NR_inotify_rm_watch	(__NR_Linux + 271)
#define __NR_migrate_pages	(__NR_Linux + 272)
#define __NR_pselect6		(__NR_Linux + 273)
#define __NR_ppoll		(__NR_Linux + 274)
#define __NR_openat		(__NR_Linux + 275)
#define __NR_mkdirat		(__NR_Linux + 276)
#define __NR_mknodat		(__NR_Linux + 277)
#define __NR_fchownat		(__NR_Linux + 278)
#define __NR_futimesat		(__NR_Linux + 279)
#define __NR_fstatat64		(__NR_Linux + 280)
#define __NR_unlinkat		(__NR_Linux + 281)
#define __NR_renameat		(__NR_Linux + 282)
#define __NR_linkat		(__NR_Linux + 283)
#define __NR_symlinkat		(__NR_Linux + 284)
#define __NR_readlinkat		(__NR_Linux + 285)
#define __NR_fchmodat		(__NR_Linux + 286)
#define __NR_faccessat		(__NR_Linux + 287)
#define __NR_unshare		(__NR_Linux + 288)
#define __NR_set_robust_list	(__NR_Linux + 289)
#define __NR_get_robust_list	(__NR_Linux + 290)
#define __NR_splice		(__NR_Linux + 291)
#define __NR_sync_file_range	(__NR_Linux + 292)
#define __NR_tee		(__NR_Linux + 293)
#define __NR_vmsplice		(__NR_Linux + 294)
#define __NR_move_pages		(__NR_Linux + 295)
#define __NR_getcpu		(__NR_Linux + 296)
#define __NR_epoll_pwait	(__NR_Linux + 297)
#define __NR_statfs64		(__NR_Linux + 298)
#define __NR_fstatfs64		(__NR_Linux + 299)
#define __NR_kexec_load		(__NR_Linux + 300)
#define __NR_utimensat		(__NR_Linux + 301)
#define __NR_signalfd		(__NR_Linux + 302)
#define __NR_timerfd		(__NR_Linux + 303)
#define __NR_eventfd		(__NR_Linux + 304)
#define __NR_fallocate		(__NR_Linux + 305)
#define __NR_timerfd_create	(__NR_Linux + 306)
#define __NR_timerfd_settime	(__NR_Linux + 307)
#define __NR_timerfd_gettime	(__NR_Linux + 308)
#define __NR_signalfd4		(__NR_Linux + 309)
#define __NR_eventfd2		(__NR_Linux + 310)
#define __NR_epoll_create1	(__NR_Linux + 311)
#define __NR_dup3		(__NR_Linux + 312)
#define __NR_pipe2		(__NR_Linux + 313)
#define __NR_inotify_init1	(__NR_Linux + 314)
#define __NR_preadv		(__NR_Linux + 315)
#define __NR_pwritev		(__NR_Linux + 316)
#define __NR_rt_tgsigqueueinfo	(__NR_Linux + 317)
#define __NR_perf_event_open	(__NR_Linux + 318)
#define __NR_recvmmsg		(__NR_Linux + 319)
#define __NR_accept4		(__NR_Linux + 320)
#define __NR_prlimit64		(__NR_Linux + 321)
#define __NR_fanotify_init	(__NR_Linux + 322)
#define __NR_fanotify_mark	(__NR_Linux + 323)
#define __NR_clock_adjtime	(__NR_Linux + 324)
#define __NR_name_to_handle_at	(__NR_Linux + 325)
#define __NR_open_by_handle_at	(__NR_Linux + 326)
#define __NR_syncfs		(__NR_Linux + 327)
#define __NR_setns		(__NR_Linux + 328)

#define __NR_Linux_syscalls	(__NR_setns + 1)


#define __IGNORE_select		/* newselect */
#define __IGNORE_fadvise64	/* fadvise64_64 */
#define __IGNORE_utimes		/* utime */


#define HPUX_GATEWAY_ADDR       0xC0000004
#define LINUX_GATEWAY_ADDR      0x100

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#define SYS_ify(syscall_name)   __NR_##syscall_name

#ifndef ASM_LINE_SEP
# define ASM_LINE_SEP ;
#endif

/* Definition taken from glibc 2.3.3
 * sysdeps/unix/sysv/linux/hppa/sysdep.h
 */

#ifdef PIC
/* WARNING: CANNOT BE USED IN A NOP! */
# define K_STW_ASM_PIC	"       copy %%r19, %%r4\n"
# define K_LDW_ASM_PIC	"       copy %%r4, %%r19\n"
# define K_USING_GR4	"%r4",
#else
# define K_STW_ASM_PIC	" \n"
# define K_LDW_ASM_PIC	" \n"
# define K_USING_GR4
#endif

/* GCC has to be warned that a syscall may clobber all the ABI
   registers listed as "caller-saves", see page 8, Table 2
   in section 2.2.6 of the PA-RISC RUN-TIME architecture
   document. However! r28 is the result and will conflict with
   the clobber list so it is left out. Also the input arguments
   registers r20 -> r26 will conflict with the list so they
   are treated specially. Although r19 is clobbered by the syscall
   we cannot say this because it would violate ABI, thus we say
   r4 is clobbered and use that register to save/restore r19
   across the syscall. */

#define K_CALL_CLOB_REGS "%r1", "%r2", K_USING_GR4 \
	        	 "%r20", "%r29", "%r31"

#undef K_INLINE_SYSCALL
#define K_INLINE_SYSCALL(name, nr, args...)	({			\
	long __sys_res;							\
	{								\
		register unsigned long __res __asm__("r28");		\
		K_LOAD_ARGS_##nr(args)					\
		/* FIXME: HACK stw/ldw r19 around syscall */		\
		__asm__ volatile(					\
			K_STW_ASM_PIC					\
			"	ble  0x100(%%sr2, %%r0)\n"		\
			"	ldi %1, %%r20\n"			\
			K_LDW_ASM_PIC					\
			: "=r" (__res)					\
			: "i" (SYS_ify(name)) K_ASM_ARGS_##nr   	\
			: "memory", K_CALL_CLOB_REGS K_CLOB_ARGS_##nr	\
		);							\
		__sys_res = (long)__res;				\
	}								\
	if ( (unsigned long)__sys_res >= (unsigned long)-4095 ){	\
		errno = -__sys_res;		        		\
		__sys_res = -1;						\
	}								\
	__sys_res;							\
})

#define K_LOAD_ARGS_0()
#define K_LOAD_ARGS_1(r26)					\
	register unsigned long __r26 __asm__("r26") = (unsigned long)(r26);   \
	K_LOAD_ARGS_0()
#define K_LOAD_ARGS_2(r26,r25)					\
	register unsigned long __r25 __asm__("r25") = (unsigned long)(r25);   \
	K_LOAD_ARGS_1(r26)
#define K_LOAD_ARGS_3(r26,r25,r24)				\
	register unsigned long __r24 __asm__("r24") = (unsigned long)(r24);   \
	K_LOAD_ARGS_2(r26,r25)
#define K_LOAD_ARGS_4(r26,r25,r24,r23)				\
	register unsigned long __r23 __asm__("r23") = (unsigned long)(r23);   \
	K_LOAD_ARGS_3(r26,r25,r24)
#define K_LOAD_ARGS_5(r26,r25,r24,r23,r22)			\
	register unsigned long __r22 __asm__("r22") = (unsigned long)(r22);   \
	K_LOAD_ARGS_4(r26,r25,r24,r23)
#define K_LOAD_ARGS_6(r26,r25,r24,r23,r22,r21)			\
	register unsigned long __r21 __asm__("r21") = (unsigned long)(r21);   \
	K_LOAD_ARGS_5(r26,r25,r24,r23,r22)

/* Even with zero args we use r20 for the syscall number */
#define K_ASM_ARGS_0
#define K_ASM_ARGS_1 K_ASM_ARGS_0, "r" (__r26)
#define K_ASM_ARGS_2 K_ASM_ARGS_1, "r" (__r25)
#define K_ASM_ARGS_3 K_ASM_ARGS_2, "r" (__r24)
#define K_ASM_ARGS_4 K_ASM_ARGS_3, "r" (__r23)
#define K_ASM_ARGS_5 K_ASM_ARGS_4, "r" (__r22)
#define K_ASM_ARGS_6 K_ASM_ARGS_5, "r" (__r21)

/* The registers not listed as inputs but clobbered */
#define K_CLOB_ARGS_6
#define K_CLOB_ARGS_5 K_CLOB_ARGS_6, "%r21"
#define K_CLOB_ARGS_4 K_CLOB_ARGS_5, "%r22"
#define K_CLOB_ARGS_3 K_CLOB_ARGS_4, "%r23"
#define K_CLOB_ARGS_2 K_CLOB_ARGS_3, "%r24"
#define K_CLOB_ARGS_1 K_CLOB_ARGS_2, "%r25"
#define K_CLOB_ARGS_0 K_CLOB_ARGS_1, "%r26"

#define _syscall0(type,name)						\
type name(void)								\
{									\
    return K_INLINE_SYSCALL(name, 0);	                                \
}

#define _syscall1(type,name,type1,arg1)					\
type name(type1 arg1)							\
{									\
    return K_INLINE_SYSCALL(name, 1, arg1);	                        \
}

#define _syscall2(type,name,type1,arg1,type2,arg2)			\
type name(type1 arg1, type2 arg2)					\
{									\
    return K_INLINE_SYSCALL(name, 2, arg1, arg2);	                \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)		\
type name(type1 arg1, type2 arg2, type3 arg3)				\
{									\
    return K_INLINE_SYSCALL(name, 3, arg1, arg2, arg3);	                \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4)		\
{									\
    return K_INLINE_SYSCALL(name, 4, arg1, arg2, arg3, arg4);	        \
}

/* select takes 5 arguments */
#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5)	\
{									\
    return K_INLINE_SYSCALL(name, 5, arg1, arg2, arg3, arg4, arg5);	\
}

#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_COMPAT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
#define __ARCH_WANT_COMPAT_SYS_RT_SIGSUSPEND

#endif /* __ASSEMBLY__ */

#undef STR

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#endif /* __KERNEL__ */
#endif /* _ASM_PARISC_UNISTD_H_ */
