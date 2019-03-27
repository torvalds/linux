# $NetBSD: d_toupper.awk,v 1.1 2012/03/11 18:36:01 jruoho Exp $

END {
	print toupper($0);
}
