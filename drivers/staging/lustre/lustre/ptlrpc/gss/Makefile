obj-$(CONFIG_LUSTRE_FS) := ptlrpc_gss.o

ptlrpc_gss-y := sec_gss.o gss_bulk.o gss_cli_upcall.o gss_svc_upcall.o	\
		gss_rawobj.o lproc_gss.o gss_generic_token.o		\
		gss_mech_switch.o gss_krb5_mech.o


ccflags-y := -I$(src)/../include
