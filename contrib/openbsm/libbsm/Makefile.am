if USE_NATIVE_INCLUDES
INCLUDES = -I$(top_builddir) -I$(top_srcdir)
else
INCLUDES = -I$(top_builddir) -I$(top_srcdir) -I$(top_srcdir)/sys
endif

lib_LTLIBRARIES = libbsm.la

libbsm_la_SOURCES =	\
	bsm_audit.c	\
	bsm_class.c	\
	bsm_control.c	\
	bsm_domain.c	\
	bsm_errno.c	\
	bsm_event.c	\
	bsm_fcntl.c	\
	bsm_flags.c	\
	bsm_io.c	\
	bsm_mask.c	\
	bsm_socket_type.c	\
	bsm_token.c	\
	bsm_user.c

if HAVE_AUDIT_SYSCALLS
libbsm_la_SOURCES +=	\
	bsm_notify.c	\
	bsm_wrappers.c
endif

man3_MANS =		\
	au_class.3	\
	au_control.3	\
	au_domain.3	\
	au_errno.3	\
	au_event.3	\
	au_fcntl_cmd.3	\
	au_free_token.3	\
	au_io.3		\
	au_mask.3	\
	au_notify.3	\
	au_open.3	\
	au_socket_type.3	\
	au_token.3	\
	au_user.3	\
	libbsm.3

install-exec-hook:
	cd $(DESTDIR)$(mandir) && \
	  $(LN_S) -f au_class.3 getauclassent.3 && \
	  $(LN_S) -f au_class.3 getauclassent_r.3 && \
	  $(LN_S) -f au_class.3 getauclassnam.3 && \
	  $(LN_S) -f au_class.3 getauclassnam_r.3 && \
	  $(LN_S) -f au_class.3 setauclass.3 && \
	  $(LN_S) -f au_class.3 endauclass.3 && \
	  $(LN_S) -f au_control.3 setac.3 && \
	  $(LN_S) -f au_control.3 endac.3 && \
	  $(LN_S) -f au_control.3 getacdir.3 && \
	  $(LN_S) -f au_control.3 getacdist.3 && \
	  $(LN_S) -f au_control.3 getacexpire.3 && \
	  $(LN_S) -f au_control.3 getacfilesz.3 && \
	  $(LN_S) -f au_control.3 getacflg.3 && \
	  $(LN_S) -f au_control.3 getachost.3 && \
	  $(LN_S) -f au_control.3 getacmin.3 && \
	  $(LN_S) -f au_control.3 getacna.3 && \
	  $(LN_S) -f au_control.3 getacpol.3 && \
	  $(LN_S) -f au_control.3 getacqsize.3 && \
	  $(LN_S) -f au_control.3 au_poltostr.3 && \
	  $(LN_S) -f au_control.3 ac_strtopol.3 && \
	  $(LN_S) -f au_domain.3 au_bsm_to_domain.3 && \
	  $(LN_S) -f au_domain.3 au_domain_to_bsm.3 && \
	  $(LN_S) -f au_errno.3 au_bsm_to_errno.3 && \
	  $(LN_S) -f au_errno.3 au_errno_to_bsm.3 && \
	  $(LN_S) -f au_errno.3 au_strerror.3 && \
	  $(LN_S) -f au_event.3 setauevent.3 && \
	  $(LN_S) -f au_event.3 endauevent.3 && \
	  $(LN_S) -f au_event.3 getauevent.3 && \
	  $(LN_S) -f au_event.3 getauevent_r.3 && \
	  $(LN_S) -f au_event.3 getauevnam.3 && \
	  $(LN_S) -f au_event.3 getauevnam_r.3 && \
	  $(LN_S) -f au_event.3 getauevnum.3 && \
	  $(LN_S) -f au_event.3 getauevnum_r.3 && \
	  $(LN_S) -f au_event.3 getauevnonam.3 && \
	  $(LN_S) -f au_event.3 getauevnonam_r.3 && \
	  $(LN_S) -f au_fcntl.3 au_bsm_to_fcntl_cmd.3 && \
	  $(LN_S) -f au_fcntl.3 au_fcntl_cmd_to_bsm.3 && \
	  $(LN_S) -f au_io.3 au_fetch_tok.3 && \
	  $(LN_S) -f au_io.3 au_print_tok.3 && \
	  $(LN_S) -f au_io.3 au_print_flags_tok.3 && \
	  $(LN_S) -f au_io.3 au_read_rec.3 && \
	  $(LN_S) -f au_mask.3 au_preselect.3 && \
	  $(LN_S) -f au_mask.3 getauditflagsbin.3 && \
	  $(LN_S) -f au_mask.3 getauditflagschar.3 && \
	  $(LN_S) -f au_notify.3 au_get_state.3 && \
	  $(LN_S) -f au_notify.3 au_notify_initialize.3 && \
	  $(LN_S) -f au_notify.3 au_notify_terminate.3 && \
	  $(LN_S) -f au_open.3 au_write.3 && \
	  $(LN_S) -f au_open.3 au_close.3 && \
	  $(LN_S) -f au_open.3 au_close_buffer.3 && \
	  $(LN_S) -f au_open.3 au_close_token.3 && \
	  $(LN_S) -f au_socket_type.3 au_bsm_to_socket_type.3 && \
	  $(LN_S) -f au_socket_type.3 au_socket_type_to_bsm.3 && \
	  $(LN_S) -f au_token.3 au_to_arg32.3 && \
	  $(LN_S) -f au_token.3 au_to_arg64.3 && \
	  $(LN_S) -f au_token.3 au_to_arg.3 && \
	  $(LN_S) -f au_token.3 au_to_attr64.3 && \
	  $(LN_S) -f au_token.3 au_to_data.3 && \
	  $(LN_S) -f au_token.3 au_to_exit.3 && \
	  $(LN_S) -f au_token.3 au_to_groups.3 && \
	  $(LN_S) -f au_token.3 au_to_newgroups.3 && \
	  $(LN_S) -f au_token.3 au_to_in_addr.3 && \
	  $(LN_S) -f au_token.3 au_to_in_addr_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_ip.3 && \
	  $(LN_S) -f au_token.3 au_to_ipc.3 && \
	  $(LN_S) -f au_token.3 au_to_ipc_perm.3 && \
	  $(LN_S) -f au_token.3 au_to_iport.3 && \
	  $(LN_S) -f au_token.3 au_to_opaque.3 && \
	  $(LN_S) -f au_token.3 au_to_file.3 && \
	  $(LN_S) -f au_token.3 au_to_text.3 && \
	  $(LN_S) -f au_token.3 au_to_path.3 && \
	  $(LN_S) -f au_token.3 au_to_process32.3 && \
	  $(LN_S) -f au_token.3 au_to_process64.3 && \
	  $(LN_S) -f au_token.3 au_to_process.3 && \
	  $(LN_S) -f au_token.3 au_to_process32_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_process64_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_process_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_return32.3 && \
	  $(LN_S) -f au_token.3 au_to_return64.3 && \
	  $(LN_S) -f au_token.3 au_to_return.3 && \
	  $(LN_S) -f au_token.3 au_to_seq.3 && \
	  $(LN_S) -f au_token.3 au_to_sock_inet32.3 && \
	  $(LN_S) -f au_token.3 au_to_sock_inet128.3 && \
	  $(LN_S) -f au_token.3 au_to_sock_inet.3 && \
	  $(LN_S) -f au_token.3 au_to_socket_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_subject32.3 && \
	  $(LN_S) -f au_token.3 au_to_subject64.3 && \
	  $(LN_S) -f au_token.3 au_to_subject.3 && \
	  $(LN_S) -f au_token.3 au_to_subject32_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_subject64_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_subject_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_me.3 && \
	  $(LN_S) -f au_token.3 au_to_exec_args.3 && \
	  $(LN_S) -f au_token.3 au_to_exec_env.3 && \
	  $(LN_S) -f au_token.3 au_to_header.3 && \
	  $(LN_S) -f au_token.3 au_to_header32.3 && \
	  $(LN_S) -f au_token.3 au_to_header64.3 && \
	  $(LN_S) -f au_token.3 au_to_header_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_header32_ex.3 && \
	  $(LN_S) -f au_token.3 au_to_trailer.3 && \
	  $(LN_S) -f au_token.3 au_to_zonename.3 && \
	  $(LN_S) -f au_user.3 setauuser.3 && \
	  $(LN_S) -f au_user.3 endauuser.3 && \
	  $(LN_S) -f au_user.3 getauuserent.3 && \
	  $(LN_S) -f au_user.3 getauuserent_r.3 && \
	  $(LN_S) -f au_user.3 getauusernam.3 && \
	  $(LN_S) -f au_user.3 getauusernam_r.3 && \
	  $(LN_S) -f au_user.3 au_user_mask.3 && \
	  $(LN_S) -f au_user.3 getfauditflags.3
