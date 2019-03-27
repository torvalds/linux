# $NetBSD: d_tolower.awk,v 1.1 2012/03/11 18:36:00 jruoho Exp $

END {
	print tolower($0);
}
