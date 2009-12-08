obj-$(CONFIG_SCSI_BFA_FC) := bfa.o

bfa-y := bfad.o bfad_intr.o bfad_os.o bfad_im.o bfad_attr.o bfad_fwimg.o

bfa-y += bfa_core.o bfa_ioc.o bfa_iocfc.o bfa_fcxp.o bfa_lps.o
bfa-y += bfa_hw_cb.o bfa_hw_ct.o bfa_intr.o bfa_timer.o bfa_rport.o 
bfa-y += bfa_fcport.o bfa_port.o bfa_uf.o bfa_sgpg.o bfa_module.o bfa_ioim.o
bfa-y += bfa_itnim.o bfa_fcpim.o bfa_tskim.o bfa_log.o bfa_log_module.o
bfa-y += bfa_csdebug.o bfa_sm.o plog.o

bfa-y += fcbuild.o fabric.o fcpim.o vfapi.o fcptm.o bfa_fcs.o bfa_fcs_port.o 
bfa-y += bfa_fcs_uf.o bfa_fcs_lport.o fab.o fdmi.o ms.o ns.o scn.o loop.o
bfa-y += lport_api.o n2n.o rport.o rport_api.o rport_ftrs.o vport.o

ccflags-y := -I$(obj) -I$(obj)/include -I$(obj)/include/cna
