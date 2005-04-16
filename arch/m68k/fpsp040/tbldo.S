|
|	tbldo.sa 3.1 12/10/90
|
| Modified:
|	8/16/90	chinds	The table was constructed to use only one level
|			of indirection in do_func for monadic
|			functions.  Dyadic functions require two
|			levels, and the tables are still contained
|			in do_func.  The table is arranged for
|			index with a 10-bit index, with the first
|			7 bits the opcode, and the remaining 3
|			the stag.  For dyadic functions, all
|			valid addresses are to the generic entry
|			point.
|

|		Copyright (C) Motorola, Inc. 1990
|			All Rights Reserved
|
|	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF MOTOROLA
|	The copyright notice above does not evidence any
|	actual or intended publication of such source code.

|TBLDO	idnt    2,1 | Motorola 040 Floating Point Software Package

	|section	8

	|xref	ld_pinf,ld_pone,ld_ppi2
	|xref	t_dz2,t_operr
	|xref	serror,sone,szero,sinf,snzrinx
	|xref	sopr_inf,spi_2,src_nan,szr_inf

	|xref	smovcr
	|xref	pmod,prem,pscale
	|xref	satanh,satanhd
	|xref	sacos,sacosd,sasin,sasind,satan,satand
	|xref	setox,setoxd,setoxm1,setoxm1d,setoxm1i
	|xref	sgetexp,sgetexpd,sgetman,sgetmand
	|xref	sint,sintd,sintrz
	|xref	ssincos,ssincosd,ssincosi,ssincosnan,ssincosz
	|xref	scos,scosd,ssin,ssind,stan,stand
	|xref	scosh,scoshd,ssinh,ssinhd,stanh,stanhd
	|xref	sslog10,sslog2,sslogn,sslognp1
	|xref	sslog10d,sslog2d,sslognd,slognp1d
	|xref	stentox,stentoxd,stwotox,stwotoxd

|	instruction		;opcode-stag Notes
	.global	tblpre
tblpre:
	.long	smovcr		|$00-0 fmovecr all
	.long	smovcr		|$00-1 fmovecr all
	.long	smovcr		|$00-2 fmovecr all
	.long	smovcr		|$00-3 fmovecr all
	.long	smovcr		|$00-4 fmovecr all
	.long	smovcr		|$00-5 fmovecr all
	.long	smovcr		|$00-6 fmovecr all
	.long	smovcr		|$00-7 fmovecr all

	.long	sint		|$01-0 fint norm
	.long	szero		|$01-1 fint zero
	.long	sinf		|$01-2 fint inf
	.long	src_nan		|$01-3 fint nan
	.long	sintd		|$01-4 fint denorm inx
	.long	serror		|$01-5 fint ERROR
	.long	serror		|$01-6 fint ERROR
	.long	serror		|$01-7 fint ERROR

	.long	ssinh		|$02-0 fsinh norm
	.long	szero		|$02-1 fsinh zero
	.long	sinf		|$02-2 fsinh inf
	.long	src_nan		|$02-3 fsinh nan
	.long	ssinhd		|$02-4 fsinh denorm
	.long	serror		|$02-5 fsinh ERROR
	.long	serror		|$02-6 fsinh ERROR
	.long	serror		|$02-7 fsinh ERROR

	.long	sintrz		|$03-0 fintrz norm
	.long	szero		|$03-1 fintrz zero
	.long	sinf		|$03-2 fintrz inf
	.long	src_nan		|$03-3 fintrz nan
	.long	snzrinx		|$03-4 fintrz denorm inx
	.long	serror		|$03-5 fintrz ERROR
	.long	serror		|$03-6 fintrz ERROR
	.long	serror		|$03-7 fintrz ERROR

	.long	serror		|$04-0 ERROR - illegal extension
	.long	serror		|$04-1 ERROR - illegal extension
	.long	serror		|$04-2 ERROR - illegal extension
	.long	serror		|$04-3 ERROR - illegal extension
	.long	serror		|$04-4 ERROR - illegal extension
	.long	serror		|$04-5 ERROR - illegal extension
	.long	serror		|$04-6 ERROR - illegal extension
	.long	serror		|$04-7 ERROR - illegal extension

	.long	serror		|$05-0 ERROR - illegal extension
	.long	serror		|$05-1 ERROR - illegal extension
	.long	serror		|$05-2 ERROR - illegal extension
	.long	serror		|$05-3 ERROR - illegal extension
	.long	serror		|$05-4 ERROR - illegal extension
	.long	serror		|$05-5 ERROR - illegal extension
	.long	serror		|$05-6 ERROR - illegal extension
	.long	serror		|$05-7 ERROR - illegal extension

	.long	sslognp1	|$06-0 flognp1 norm
	.long	szero		|$06-1 flognp1 zero
	.long	sopr_inf	|$06-2 flognp1 inf
	.long	src_nan		|$06-3 flognp1 nan
	.long	slognp1d	|$06-4 flognp1 denorm
	.long	serror		|$06-5 flognp1 ERROR
	.long	serror		|$06-6 flognp1 ERROR
	.long	serror		|$06-7 flognp1 ERROR

	.long	serror		|$07-0 ERROR - illegal extension
	.long	serror		|$07-1 ERROR - illegal extension
	.long	serror		|$07-2 ERROR - illegal extension
	.long	serror		|$07-3 ERROR - illegal extension
	.long	serror		|$07-4 ERROR - illegal extension
	.long	serror		|$07-5 ERROR - illegal extension
	.long	serror		|$07-6 ERROR - illegal extension
	.long	serror		|$07-7 ERROR - illegal extension

	.long	setoxm1		|$08-0 fetoxm1 norm
	.long	szero		|$08-1 fetoxm1 zero
	.long	setoxm1i	|$08-2 fetoxm1 inf
	.long	src_nan		|$08-3 fetoxm1 nan
	.long	setoxm1d	|$08-4 fetoxm1 denorm
	.long	serror		|$08-5 fetoxm1 ERROR
	.long	serror		|$08-6 fetoxm1 ERROR
	.long	serror		|$08-7 fetoxm1 ERROR

	.long	stanh		|$09-0 ftanh norm
	.long	szero		|$09-1 ftanh zero
	.long	sone		|$09-2 ftanh inf
	.long	src_nan		|$09-3 ftanh nan
	.long	stanhd		|$09-4 ftanh denorm
	.long	serror		|$09-5 ftanh ERROR
	.long	serror		|$09-6 ftanh ERROR
	.long	serror		|$09-7 ftanh ERROR

	.long	satan		|$0a-0 fatan norm
	.long	szero		|$0a-1 fatan zero
	.long	spi_2		|$0a-2 fatan inf
	.long	src_nan		|$0a-3 fatan nan
	.long	satand		|$0a-4 fatan denorm
	.long	serror		|$0a-5 fatan ERROR
	.long	serror		|$0a-6 fatan ERROR
	.long	serror		|$0a-7 fatan ERROR

	.long	serror		|$0b-0 ERROR - illegal extension
	.long	serror		|$0b-1 ERROR - illegal extension
	.long	serror		|$0b-2 ERROR - illegal extension
	.long	serror		|$0b-3 ERROR - illegal extension
	.long	serror		|$0b-4 ERROR - illegal extension
	.long	serror		|$0b-5 ERROR - illegal extension
	.long	serror		|$0b-6 ERROR - illegal extension
	.long	serror		|$0b-7 ERROR - illegal extension

	.long	sasin		|$0c-0 fasin norm
	.long	szero		|$0c-1 fasin zero
	.long	t_operr		|$0c-2 fasin inf
	.long	src_nan		|$0c-3 fasin nan
	.long	sasind		|$0c-4 fasin denorm
	.long	serror		|$0c-5 fasin ERROR
	.long	serror		|$0c-6 fasin ERROR
	.long	serror		|$0c-7 fasin ERROR

	.long	satanh		|$0d-0 fatanh norm
	.long	szero		|$0d-1 fatanh zero
	.long	t_operr		|$0d-2 fatanh inf
	.long	src_nan		|$0d-3 fatanh nan
	.long	satanhd		|$0d-4 fatanh denorm
	.long	serror		|$0d-5 fatanh ERROR
	.long	serror		|$0d-6 fatanh ERROR
	.long	serror		|$0d-7 fatanh ERROR

	.long	ssin		|$0e-0 fsin norm
	.long	szero		|$0e-1 fsin zero
	.long	t_operr		|$0e-2 fsin inf
	.long	src_nan		|$0e-3 fsin nan
	.long	ssind		|$0e-4 fsin denorm
	.long	serror		|$0e-5 fsin ERROR
	.long	serror		|$0e-6 fsin ERROR
	.long	serror		|$0e-7 fsin ERROR

	.long	stan		|$0f-0 ftan norm
	.long	szero		|$0f-1 ftan zero
	.long	t_operr		|$0f-2 ftan inf
	.long	src_nan		|$0f-3 ftan nan
	.long	stand		|$0f-4 ftan denorm
	.long	serror		|$0f-5 ftan ERROR
	.long	serror		|$0f-6 ftan ERROR
	.long	serror		|$0f-7 ftan ERROR

	.long	setox		|$10-0 fetox norm
	.long	ld_pone		|$10-1 fetox zero
	.long	szr_inf		|$10-2 fetox inf
	.long	src_nan		|$10-3 fetox nan
	.long	setoxd		|$10-4 fetox denorm
	.long	serror		|$10-5 fetox ERROR
	.long	serror		|$10-6 fetox ERROR
	.long	serror		|$10-7 fetox ERROR

	.long	stwotox		|$11-0 ftwotox norm
	.long	ld_pone		|$11-1 ftwotox zero
	.long	szr_inf		|$11-2 ftwotox inf
	.long	src_nan		|$11-3 ftwotox nan
	.long	stwotoxd	|$11-4 ftwotox denorm
	.long	serror		|$11-5 ftwotox ERROR
	.long	serror		|$11-6 ftwotox ERROR
	.long	serror		|$11-7 ftwotox ERROR

	.long	stentox		|$12-0 ftentox norm
	.long	ld_pone		|$12-1 ftentox zero
	.long	szr_inf		|$12-2 ftentox inf
	.long	src_nan		|$12-3 ftentox nan
	.long	stentoxd	|$12-4 ftentox denorm
	.long	serror		|$12-5 ftentox ERROR
	.long	serror		|$12-6 ftentox ERROR
	.long	serror		|$12-7 ftentox ERROR

	.long	serror		|$13-0 ERROR - illegal extension
	.long	serror		|$13-1 ERROR - illegal extension
	.long	serror		|$13-2 ERROR - illegal extension
	.long	serror		|$13-3 ERROR - illegal extension
	.long	serror		|$13-4 ERROR - illegal extension
	.long	serror		|$13-5 ERROR - illegal extension
	.long	serror		|$13-6 ERROR - illegal extension
	.long	serror		|$13-7 ERROR - illegal extension

	.long	sslogn		|$14-0 flogn norm
	.long	t_dz2		|$14-1 flogn zero
	.long	sopr_inf	|$14-2 flogn inf
	.long	src_nan		|$14-3 flogn nan
	.long	sslognd		|$14-4 flogn denorm
	.long	serror		|$14-5 flogn ERROR
	.long	serror		|$14-6 flogn ERROR
	.long	serror		|$14-7 flogn ERROR

	.long	sslog10		|$15-0 flog10 norm
	.long	t_dz2		|$15-1 flog10 zero
	.long	sopr_inf	|$15-2 flog10 inf
	.long	src_nan		|$15-3 flog10 nan
	.long	sslog10d	|$15-4 flog10 denorm
	.long	serror		|$15-5 flog10 ERROR
	.long	serror		|$15-6 flog10 ERROR
	.long	serror		|$15-7 flog10 ERROR

	.long	sslog2		|$16-0 flog2 norm
	.long	t_dz2		|$16-1 flog2 zero
	.long	sopr_inf	|$16-2 flog2 inf
	.long	src_nan		|$16-3 flog2 nan
	.long	sslog2d		|$16-4 flog2 denorm
	.long	serror		|$16-5 flog2 ERROR
	.long	serror		|$16-6 flog2 ERROR
	.long	serror		|$16-7 flog2 ERROR

	.long	serror		|$17-0 ERROR - illegal extension
	.long	serror		|$17-1 ERROR - illegal extension
	.long	serror		|$17-2 ERROR - illegal extension
	.long	serror		|$17-3 ERROR - illegal extension
	.long	serror		|$17-4 ERROR - illegal extension
	.long	serror		|$17-5 ERROR - illegal extension
	.long	serror		|$17-6 ERROR - illegal extension
	.long	serror		|$17-7 ERROR - illegal extension

	.long	serror		|$18-0 ERROR - illegal extension
	.long	serror		|$18-1 ERROR - illegal extension
	.long	serror		|$18-2 ERROR - illegal extension
	.long	serror		|$18-3 ERROR - illegal extension
	.long	serror		|$18-4 ERROR - illegal extension
	.long	serror		|$18-5 ERROR - illegal extension
	.long	serror		|$18-6 ERROR - illegal extension
	.long	serror		|$18-7 ERROR - illegal extension

	.long	scosh		|$19-0 fcosh norm
	.long	ld_pone		|$19-1 fcosh zero
	.long	ld_pinf		|$19-2 fcosh inf
	.long	src_nan		|$19-3 fcosh nan
	.long	scoshd		|$19-4 fcosh denorm
	.long	serror		|$19-5 fcosh ERROR
	.long	serror		|$19-6 fcosh ERROR
	.long	serror		|$19-7 fcosh ERROR

	.long	serror		|$1a-0 ERROR - illegal extension
	.long	serror		|$1a-1 ERROR - illegal extension
	.long	serror		|$1a-2 ERROR - illegal extension
	.long	serror		|$1a-3 ERROR - illegal extension
	.long	serror		|$1a-4 ERROR - illegal extension
	.long	serror		|$1a-5 ERROR - illegal extension
	.long	serror		|$1a-6 ERROR - illegal extension
	.long	serror		|$1a-7 ERROR - illegal extension

	.long	serror		|$1b-0 ERROR - illegal extension
	.long	serror		|$1b-1 ERROR - illegal extension
	.long	serror		|$1b-2 ERROR - illegal extension
	.long	serror		|$1b-3 ERROR - illegal extension
	.long	serror		|$1b-4 ERROR - illegal extension
	.long	serror		|$1b-5 ERROR - illegal extension
	.long	serror		|$1b-6 ERROR - illegal extension
	.long	serror		|$1b-7 ERROR - illegal extension

	.long	sacos		|$1c-0 facos norm
	.long	ld_ppi2		|$1c-1 facos zero
	.long	t_operr		|$1c-2 facos inf
	.long	src_nan		|$1c-3 facos nan
	.long	sacosd		|$1c-4 facos denorm
	.long	serror		|$1c-5 facos ERROR
	.long	serror		|$1c-6 facos ERROR
	.long	serror		|$1c-7 facos ERROR

	.long	scos		|$1d-0 fcos norm
	.long	ld_pone		|$1d-1 fcos zero
	.long	t_operr		|$1d-2 fcos inf
	.long	src_nan		|$1d-3 fcos nan
	.long	scosd		|$1d-4 fcos denorm
	.long	serror		|$1d-5 fcos ERROR
	.long	serror		|$1d-6 fcos ERROR
	.long	serror		|$1d-7 fcos ERROR

	.long	sgetexp		|$1e-0 fgetexp norm
	.long	szero		|$1e-1 fgetexp zero
	.long	t_operr		|$1e-2 fgetexp inf
	.long	src_nan		|$1e-3 fgetexp nan
	.long	sgetexpd	|$1e-4 fgetexp denorm
	.long	serror		|$1e-5 fgetexp ERROR
	.long	serror		|$1e-6 fgetexp ERROR
	.long	serror		|$1e-7 fgetexp ERROR

	.long	sgetman		|$1f-0 fgetman norm
	.long	szero		|$1f-1 fgetman zero
	.long	t_operr		|$1f-2 fgetman inf
	.long	src_nan		|$1f-3 fgetman nan
	.long	sgetmand	|$1f-4 fgetman denorm
	.long	serror		|$1f-5 fgetman ERROR
	.long	serror		|$1f-6 fgetman ERROR
	.long	serror		|$1f-7 fgetman ERROR

	.long	serror		|$20-0 ERROR - illegal extension
	.long	serror		|$20-1 ERROR - illegal extension
	.long	serror		|$20-2 ERROR - illegal extension
	.long	serror		|$20-3 ERROR - illegal extension
	.long	serror		|$20-4 ERROR - illegal extension
	.long	serror		|$20-5 ERROR - illegal extension
	.long	serror		|$20-6 ERROR - illegal extension
	.long	serror		|$20-7 ERROR - illegal extension

	.long	pmod		|$21-0 fmod all
	.long	pmod		|$21-1 fmod all
	.long	pmod		|$21-2 fmod all
	.long	pmod		|$21-3 fmod all
	.long	pmod		|$21-4 fmod all
	.long	serror		|$21-5 fmod ERROR
	.long	serror		|$21-6 fmod ERROR
	.long	serror		|$21-7 fmod ERROR

	.long	serror		|$22-0 ERROR - illegal extension
	.long	serror		|$22-1 ERROR - illegal extension
	.long	serror		|$22-2 ERROR - illegal extension
	.long	serror		|$22-3 ERROR - illegal extension
	.long	serror		|$22-4 ERROR - illegal extension
	.long	serror		|$22-5 ERROR - illegal extension
	.long	serror		|$22-6 ERROR - illegal extension
	.long	serror		|$22-7 ERROR - illegal extension

	.long	serror		|$23-0 ERROR - illegal extension
	.long	serror		|$23-1 ERROR - illegal extension
	.long	serror		|$23-2 ERROR - illegal extension
	.long	serror		|$23-3 ERROR - illegal extension
	.long	serror		|$23-4 ERROR - illegal extension
	.long	serror		|$23-5 ERROR - illegal extension
	.long	serror		|$23-6 ERROR - illegal extension
	.long	serror		|$23-7 ERROR - illegal extension

	.long	serror		|$24-0 ERROR - illegal extension
	.long	serror		|$24-1 ERROR - illegal extension
	.long	serror		|$24-2 ERROR - illegal extension
	.long	serror		|$24-3 ERROR - illegal extension
	.long	serror		|$24-4 ERROR - illegal extension
	.long	serror		|$24-5 ERROR - illegal extension
	.long	serror		|$24-6 ERROR - illegal extension
	.long	serror		|$24-7 ERROR - illegal extension

	.long	prem		|$25-0 frem all
	.long	prem		|$25-1 frem all
	.long	prem		|$25-2 frem all
	.long	prem		|$25-3 frem all
	.long	prem		|$25-4 frem all
	.long	serror		|$25-5 frem ERROR
	.long	serror		|$25-6 frem ERROR
	.long	serror		|$25-7 frem ERROR

	.long	pscale		|$26-0 fscale all
	.long	pscale		|$26-1 fscale all
	.long	pscale		|$26-2 fscale all
	.long	pscale		|$26-3 fscale all
	.long	pscale		|$26-4 fscale all
	.long	serror		|$26-5 fscale ERROR
	.long	serror		|$26-6 fscale ERROR
	.long	serror		|$26-7 fscale ERROR

	.long	serror		|$27-0 ERROR - illegal extension
	.long	serror		|$27-1 ERROR - illegal extension
	.long	serror		|$27-2 ERROR - illegal extension
	.long	serror		|$27-3 ERROR - illegal extension
	.long	serror		|$27-4 ERROR - illegal extension
	.long	serror		|$27-5 ERROR - illegal extension
	.long	serror		|$27-6 ERROR - illegal extension
	.long	serror		|$27-7 ERROR - illegal extension

	.long	serror		|$28-0 ERROR - illegal extension
	.long	serror		|$28-1 ERROR - illegal extension
	.long	serror		|$28-2 ERROR - illegal extension
	.long	serror		|$28-3 ERROR - illegal extension
	.long	serror		|$28-4 ERROR - illegal extension
	.long	serror		|$28-5 ERROR - illegal extension
	.long	serror		|$28-6 ERROR - illegal extension
	.long	serror		|$28-7 ERROR - illegal extension

	.long	serror		|$29-0 ERROR - illegal extension
	.long	serror		|$29-1 ERROR - illegal extension
	.long	serror		|$29-2 ERROR - illegal extension
	.long	serror		|$29-3 ERROR - illegal extension
	.long	serror		|$29-4 ERROR - illegal extension
	.long	serror		|$29-5 ERROR - illegal extension
	.long	serror		|$29-6 ERROR - illegal extension
	.long	serror		|$29-7 ERROR - illegal extension

	.long	serror		|$2a-0 ERROR - illegal extension
	.long	serror		|$2a-1 ERROR - illegal extension
	.long	serror		|$2a-2 ERROR - illegal extension
	.long	serror		|$2a-3 ERROR - illegal extension
	.long	serror		|$2a-4 ERROR - illegal extension
	.long	serror		|$2a-5 ERROR - illegal extension
	.long	serror		|$2a-6 ERROR - illegal extension
	.long	serror		|$2a-7 ERROR - illegal extension

	.long	serror		|$2b-0 ERROR - illegal extension
	.long	serror		|$2b-1 ERROR - illegal extension
	.long	serror		|$2b-2 ERROR - illegal extension
	.long	serror		|$2b-3 ERROR - illegal extension
	.long	serror		|$2b-4 ERROR - illegal extension
	.long	serror		|$2b-5 ERROR - illegal extension
	.long	serror		|$2b-6 ERROR - illegal extension
	.long	serror		|$2b-7 ERROR - illegal extension

	.long	serror		|$2c-0 ERROR - illegal extension
	.long	serror		|$2c-1 ERROR - illegal extension
	.long	serror		|$2c-2 ERROR - illegal extension
	.long	serror		|$2c-3 ERROR - illegal extension
	.long	serror		|$2c-4 ERROR - illegal extension
	.long	serror		|$2c-5 ERROR - illegal extension
	.long	serror		|$2c-6 ERROR - illegal extension
	.long	serror		|$2c-7 ERROR - illegal extension

	.long	serror		|$2d-0 ERROR - illegal extension
	.long	serror		|$2d-1 ERROR - illegal extension
	.long	serror		|$2d-2 ERROR - illegal extension
	.long	serror		|$2d-3 ERROR - illegal extension
	.long	serror		|$2d-4 ERROR - illegal extension
	.long	serror		|$2d-5 ERROR - illegal extension
	.long	serror		|$2d-6 ERROR - illegal extension
	.long	serror		|$2d-7 ERROR - illegal extension

	.long	serror		|$2e-0 ERROR - illegal extension
	.long	serror		|$2e-1 ERROR - illegal extension
	.long	serror		|$2e-2 ERROR - illegal extension
	.long	serror		|$2e-3 ERROR - illegal extension
	.long	serror		|$2e-4 ERROR - illegal extension
	.long	serror		|$2e-5 ERROR - illegal extension
	.long	serror		|$2e-6 ERROR - illegal extension
	.long	serror		|$2e-7 ERROR - illegal extension

	.long	serror		|$2f-0 ERROR - illegal extension
	.long	serror		|$2f-1 ERROR - illegal extension
	.long	serror		|$2f-2 ERROR - illegal extension
	.long	serror		|$2f-3 ERROR - illegal extension
	.long	serror		|$2f-4 ERROR - illegal extension
	.long	serror		|$2f-5 ERROR - illegal extension
	.long	serror		|$2f-6 ERROR - illegal extension
	.long	serror		|$2f-7 ERROR - illegal extension

	.long	ssincos		|$30-0 fsincos norm
	.long	ssincosz	|$30-1 fsincos zero
	.long	ssincosi	|$30-2 fsincos inf
	.long	ssincosnan	|$30-3 fsincos nan
	.long	ssincosd	|$30-4 fsincos denorm
	.long	serror		|$30-5 fsincos ERROR
	.long	serror		|$30-6 fsincos ERROR
	.long	serror		|$30-7 fsincos ERROR

	.long	ssincos		|$31-0 fsincos norm
	.long	ssincosz	|$31-1 fsincos zero
	.long	ssincosi	|$31-2 fsincos inf
	.long	ssincosnan	|$31-3 fsincos nan
	.long	ssincosd	|$31-4 fsincos denorm
	.long	serror		|$31-5 fsincos ERROR
	.long	serror		|$31-6 fsincos ERROR
	.long	serror		|$31-7 fsincos ERROR

	.long	ssincos		|$32-0 fsincos norm
	.long	ssincosz	|$32-1 fsincos zero
	.long	ssincosi	|$32-2 fsincos inf
	.long	ssincosnan	|$32-3 fsincos nan
	.long	ssincosd	|$32-4 fsincos denorm
	.long	serror		|$32-5 fsincos ERROR
	.long	serror		|$32-6 fsincos ERROR
	.long	serror		|$32-7 fsincos ERROR

	.long	ssincos		|$33-0 fsincos norm
	.long	ssincosz	|$33-1 fsincos zero
	.long	ssincosi	|$33-2 fsincos inf
	.long	ssincosnan	|$33-3 fsincos nan
	.long	ssincosd	|$33-4 fsincos denorm
	.long	serror		|$33-5 fsincos ERROR
	.long	serror		|$33-6 fsincos ERROR
	.long	serror		|$33-7 fsincos ERROR

	.long	ssincos		|$34-0 fsincos norm
	.long	ssincosz	|$34-1 fsincos zero
	.long	ssincosi	|$34-2 fsincos inf
	.long	ssincosnan	|$34-3 fsincos nan
	.long	ssincosd	|$34-4 fsincos denorm
	.long	serror		|$34-5 fsincos ERROR
	.long	serror		|$34-6 fsincos ERROR
	.long	serror		|$34-7 fsincos ERROR

	.long	ssincos		|$35-0 fsincos norm
	.long	ssincosz	|$35-1 fsincos zero
	.long	ssincosi	|$35-2 fsincos inf
	.long	ssincosnan	|$35-3 fsincos nan
	.long	ssincosd	|$35-4 fsincos denorm
	.long	serror		|$35-5 fsincos ERROR
	.long	serror		|$35-6 fsincos ERROR
	.long	serror		|$35-7 fsincos ERROR

	.long	ssincos		|$36-0 fsincos norm
	.long	ssincosz	|$36-1 fsincos zero
	.long	ssincosi	|$36-2 fsincos inf
	.long	ssincosnan	|$36-3 fsincos nan
	.long	ssincosd	|$36-4 fsincos denorm
	.long	serror		|$36-5 fsincos ERROR
	.long	serror		|$36-6 fsincos ERROR
	.long	serror		|$36-7 fsincos ERROR

	.long	ssincos		|$37-0 fsincos norm
	.long	ssincosz	|$37-1 fsincos zero
	.long	ssincosi	|$37-2 fsincos inf
	.long	ssincosnan	|$37-3 fsincos nan
	.long	ssincosd	|$37-4 fsincos denorm
	.long	serror		|$37-5 fsincos ERROR
	.long	serror		|$37-6 fsincos ERROR
	.long	serror		|$37-7 fsincos ERROR

	|end
