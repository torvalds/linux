#	$Id: Makefile,v 9.0 2012/10/19 15:13:11 zy Exp $

CAT=	dutch english french german polish ru_RU.KOI8-R spanish swedish \
	uk_UA.KOI8-U zh_CN.GB2312
FILES=	../cl/*.c ../common/*.c ../ex/*.c ../vi/*.c

all: dump ${CAT}

${CAT}: english.base
	@echo "... $@";							\
	rm -f $@;							\
	sort -u $@.base |						\
	awk '{								\
		if ($$1 == 1) {						\
			print "\nMESSAGE NUMBER 1 IS NOT LEGAL";	\
			exit 1;						\
		}							\
		if (++nline > $$1) {					\
			print "DUPLICATE MESSAGE NUMBER " $$1;		\
			exit 1;						\
		}							\
		print $0;						\
	}' |								\
	sed -e '1s/^/$$set 1~$$quote "~/; 1y/~/\n/' |			\
	gencat $@ /dev/stdin;						\
	chmod 444 $@;							\
	if grep DUPLICATE $@ > /dev/null; then				\
		grep DUPLICATE $@;					\
	fi;								\
	if grep 'NOT LEGAL' $@ > /dev/null; then			\
		grep 'NOT LEGAL' $@;					\
	fi

CHK=	dutch.check english.check french.check german.check \
	polish.check ru_RU.KOI8-R.check spanish.check swedish.check \
	uk_UA.KOI8-U.check zh_CN.GB2312.check
check: ${CHK}
${CHK}: ${CAT}
	@echo "... $@";							\
	f=`basename $@ .check`;						\
	(echo "Unused message id's (this is okay):";			\
	awk '{								\
		while (++nline < $$1)					\
			printf "%03d\n", nline;				\
	}' < $$f.base;							\
	echo =========================;					\
	echo "MISSING ERROR MESSAGES (Please add!):";			\
	awk '{print $$1}' < $$f.base | sort -u > __ck1;			\
	awk '{print $$1}' < english.base | sort -u > __ck2;		\
	comm -13 __ck1 __ck2;						\
	echo =========================;					\
	echo "Extra error messages (just delete them):";		\
	comm -23 __ck1 __ck2;						\
	echo =========================;					\
	echo "MESSAGES WITH THE SAME MESSAGE ID's (FIX!):";		\
	for j in							\
	    `sed '/^$$/d' < $$f.base | sort -u |			\
	    awk '{print $$1}' | uniq -d`; do				\
		egrep $$j $$f.base;					\
	done;								\
	echo =========================;					\
	echo "Duplicate messages, both id and message (this is okay):";	\
	sed '/^$$/d' < $$f.base | sort | uniq -c |			\
	awk '$$1 != 1 { print $$0 }' | sort -n;				\
	echo =========================) > $@

english.base: dump ${FILES} #Makefile
	./dump ${FILES} |\
	sed -e '/|/!d' \
	    -e 's/|/ "/' \
	    -e 's/^"//' |\
	sort -nu > $@

dump: dump.c
	${CC} -O -o dump dump.c

clean:
	rm -f dump dump.o ${CAT} english.base *.check __ck1 __ck2
