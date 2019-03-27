# $NetBSD: d_assign_NF.awk,v 1.1 2012/03/11 18:35:59 jruoho Exp $

{
	NF = 2
	print "$0=`" $0 "`"
	print "$3=`" $3 "`"
	print "$4=`" $4 "`"
	NF = 3
	print "$0=`" $0 "`"
	print "$3=`" $3 "`"
	print "$4=`" $4 "`"
	NF = 4
	print "$0=`" $0 "`"
	print "$3=`" $3 "`"
	print "$4=`" $4 "`"
}
