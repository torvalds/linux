#!/bin/ksh

export TZ=Etc/Universal

datesub() {
	gdate "$@" '+	FILL(%_11s,%_4Y,%_m,%_d,%w,%_H,%_M,%_S), // %a %b %e %H:%M:%S %Z %Y'
}

(
	datesub -d '1970/01/01 00:00:00'
	datesub -d '1981/04/12 12:00:03'
	datesub -d '2011/07/21 09:57:00'
	datesub -d @2147483647
	datesub -d @2147483648
	datesub -d '2063/04/05 00:00:00'
	for year in `seq 1970 1 2030`; do
		datesub -d "${year}/01/01 00:00:00"
		datesub -d "${year}/07/01 00:00:00"
	done
	for year in `seq 2000 25 2600`; do
		datesub -d "$((${year} - 1))/12/31 23:59:59"
		datesub -d "$((${year} + 0))/01/01 00:00:00"
		datesub -d "$((${year} + 1))/01/01 00:00:00"
	done
)|sort -u
