# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ACPICA Core interpreter
#

ccflags-y			:= -D_LINUX -DBUILDING_ACPICA
ccflags-$(CONFIG_ACPI_DEBUG)	+= -DACPI_DEBUG_OUTPUT

# use acpi.o to put all files here into acpi.o modparam namespace
obj-y	+= acpi.o

acpi-y :=		\
	dsargs.o	\
	dscontrol.o	\
	dsdebug.o	\
	dsfield.o	\
	dsinit.o	\
	dsmethod.o	\
	dsmthdat.o	\
	dsobject.o	\
	dsopcode.o	\
	dspkginit.o	\
	dsutils.o	\
	dswexec.o	\
	dswload.o	\
	dswload2.o	\
	dswscope.o	\
	dswstate.o

acpi-y +=		\
	evevent.o	\
	evgpe.o		\
	evgpeblk.o	\
	evgpeinit.o	\
	evgpeutil.o	\
	evglock.o	\
	evhandler.o	\
	evmisc.o	\
	evregion.o	\
	evrgnini.o	\
	evsci.o		\
	evxface.o	\
	evxfevnt.o	\
	evxfgpe.o	\
	evxfregn.o

acpi-y +=		\
	exconcat.o	\
	exconfig.o	\
	exconvrt.o	\
	excreate.o	\
	exdebug.o	\
	exdump.o	\
	exfield.o	\
	exfldio.o	\
	exmisc.o	\
	exmutex.o	\
	exnames.o	\
	exoparg1.o	\
	exoparg2.o	\
	exoparg3.o	\
	exoparg6.o	\
	exprep.o	\
	exregion.o	\
	exresnte.o	\
	exresolv.o	\
	exresop.o	\
	exserial.o	\
	exstore.o	\
	exstoren.o	\
	exstorob.o	\
	exsystem.o	\
	extrace.o	\
	exutils.o

acpi-y +=		\
	hwacpi.o	\
	hwesleep.o	\
	hwgpe.o		\
	hwregs.o	\
	hwsleep.o	\
	hwvalid.o	\
	hwxface.o	\
	hwxfsleep.o

acpi-$(CONFIG_PCI) += hwpci.o
acpi-$(ACPI_FUTURE_USAGE) += hwtimer.o

acpi-y +=		\
	nsaccess.o	\
	nsalloc.o	\
	nsarguments.o	\
	nsconvert.o	\
	nsdump.o	\
	nseval.o	\
	nsinit.o	\
	nsload.o	\
	nsnames.o	\
	nsobject.o	\
	nsparse.o	\
	nspredef.o	\
	nsprepkg.o	\
	nsrepair.o	\
	nsrepair2.o	\
	nssearch.o	\
	nsutils.o	\
	nswalk.o	\
	nsxfeval.o	\
	nsxfname.o	\
	nsxfobj.o

acpi-$(ACPI_FUTURE_USAGE) += nsdumpdv.o

acpi-y +=		\
	psargs.o	\
	psloop.o	\
	psobject.o	\
	psopcode.o	\
	psopinfo.o	\
	psparse.o	\
	psscope.o	\
	pstree.o	\
	psutils.o	\
	pswalk.o	\
	psxface.o

acpi-y +=		\
	rsaddr.o	\
	rscalc.o	\
	rscreate.o	\
	rsdumpinfo.o	\
	rsinfo.o	\
	rsio.o		\
	rsirq.o		\
	rslist.o	\
	rsmemory.o	\
	rsmisc.o	\
	rsserial.o	\
	rsutils.o	\
	rsxface.o

acpi-y +=		\
	tbdata.o	\
	tbfadt.o	\
	tbfind.o	\
	tbinstal.o	\
	tbprint.o	\
	tbutils.o	\
	tbxface.o	\
	tbxfload.o	\
	tbxfroot.o

acpi-y +=		\
	utaddress.o	\
	utalloc.o	\
	utascii.o	\
	utbuffer.o	\
	utcopy.o	\
	utexcep.o	\
	utdebug.o	\
	utdecode.o	\
	utdelete.o	\
	uterror.o	\
	uteval.o	\
	utglobal.o	\
	uthex.o		\
	utids.o		\
	utinit.o	\
	utlock.o	\
	utmath.o	\
	utmisc.o	\
	utmutex.o	\
	utnonansi.o	\
	utobject.o	\
	utosi.o		\
	utownerid.o	\
	utpredef.o	\
	utresdecode.o	\
	utresrc.o	\
	utstate.o	\
	utstring.o	\
	utstrsuppt.o	\
	utstrtoul64.o	\
	utxface.o	\
	utxfinit.o	\
	utxferror.o	\
	utxfmutex.o

acpi-$(CONFIG_ACPI_DEBUGGER) +=	\
	dbcmds.o		\
	dbconvert.o		\
	dbdisply.o		\
	dbexec.o		\
	dbhistry.o		\
	dbinput.o		\
	dbmethod.o		\
	dbnames.o		\
	dbobject.o		\
	dbstats.o		\
	dbutils.o		\
	dbxface.o		\
	rsdump.o		\

acpi-$(ACPI_FUTURE_USAGE) +=	\
	dbfileio.o		\
	dbtest.o		\
	utcache.o		\
	utprint.o		\
	uttrack.o		\
	utuuid.o

