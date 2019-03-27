man2_MANS =			\
	audit.2			\
	auditctl.2		\
	auditon.2		\
	getaudit.2		\
	getauid.2		\
	setaudit.2		\
	setauid.2

man5_MANS =			\
	audit.log.5		\
	audit_class.5		\
	audit_control.5		\
	audit_event.5		\
	audit_user.5		\
	audit_warn.5

install-exec-hook:
	cd $(DESTDIR)$(mandir) && \
          $(LN_S) -f getaudit.2 getaudit_addr.2 && \
          $(LN_S) -f setaudit.2 setaudit_addr.2
