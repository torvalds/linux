# Check tz tables for consistency.

# Contributed by Paul Eggert.  This file is in the public domain.

BEGIN {
	FS = "\t"

	if (!iso_table) iso_table = "iso3166.tab"
	if (!zone_table) zone_table = "zone1970.tab"
	if (!want_warnings) want_warnings = -1

	while (getline <iso_table) {
		iso_NR++
		if ($0 ~ /^#/) continue
		if (NF != 2) {
			printf "%s:%d: wrong number of columns\n", \
				iso_table, iso_NR >>"/dev/stderr"
			status = 1
		}
		cc = $1
		name = $2
		if (cc !~ /^[A-Z][A-Z]$/) {
			printf "%s:%d: invalid country code '%s'\n", \
				iso_table, iso_NR, cc >>"/dev/stderr"
			status = 1
		}
		if (cc <= cc0) {
			if (cc == cc0) {
				s = "duplicate";
			} else {
				s = "out of order";
			}

			printf "%s:%d: country code '%s' is %s\n", \
				iso_table, iso_NR, cc, s \
				>>"/dev/stderr"
			status = 1
		}
		cc0 = cc
		if (name2cc[name]) {
			printf "%s:%d: '%s' and '%s' have the same name\n", \
				iso_table, iso_NR, name2cc[name], cc \
				>>"/dev/stderr"
			status = 1
		}
		name2cc[name] = cc
		cc2name[cc] = name
		cc2NR[cc] = iso_NR
	}

	cc0 = ""

	while (getline <zone_table) {
		zone_NR++
		if ($0 ~ /^#/) continue
		if (NF != 3 && NF != 4) {
			printf "%s:%d: wrong number of columns\n", \
				zone_table, zone_NR >>"/dev/stderr"
			status = 1
		}
		split($1, cca, /,/)
		cc = cca[1]
		coordinates = $2
		tz = $3
		comments = $4
		if (cc < cc0) {
			printf "%s:%d: country code '%s' is out of order\n", \
				zone_table, zone_NR, cc >>"/dev/stderr"
			status = 1
		}
		cc0 = cc
		tztab[tz] = 1
		tz2comments[tz] = comments
		tz2NR[tz] = zone_NR
		for (i in cca) {
		    cc = cca[i]
		    cctz = cc tz
		    cctztab[cctz] = 1
		    if (cc2name[cc]) {
			cc_used[cc]++
		    } else {
			printf "%s:%d: %s: unknown country code\n", \
				zone_table, zone_NR, cc >>"/dev/stderr"
			status = 1
		    }
		}
		if (coordinates !~ /^[-+][0-9][0-9][0-5][0-9][-+][01][0-9][0-9][0-5][0-9]$/ \
		    && coordinates !~ /^[-+][0-9][0-9][0-5][0-9][0-5][0-9][-+][01][0-9][0-9][0-5][0-9][0-5][0-9]$/) {
			printf "%s:%d: %s: invalid coordinates\n", \
				zone_table, zone_NR, coordinates >>"/dev/stderr"
			status = 1
		}
	}

	for (cctz in cctztab) {
		cc = substr (cctz, 1, 2)
		tz = substr (cctz, 3)
		if (1 < cc_used[cc]) {
			comments_needed[tz] = cc
		}
	}
	for (cctz in cctztab) {
	  cc = substr (cctz, 1, 2)
	  tz = substr (cctz, 3)
	  if (!comments_needed[tz] && tz2comments[tz]) {
	    printf "%s:%d: unnecessary comment '%s'\n", \
		zone_table, tz2NR[tz], tz2comments[tz] \
		>>"/dev/stderr"
	    tz2comments[tz] = 0
	    status = 1
	  } else if (comments_needed[tz] && !tz2comments[tz]) {
	    printf "%s:%d: missing comment for %s\n", \
	      zone_table, tz2NR[tz], comments_needed[tz] \
	      >>"/dev/stderr"
	    tz2comments[tz] = 1
	    status = 1
	  }
	}
	FS = " "
}

$1 ~ /^#/ { next }

{
	tz = rules = ""
	if ($1 == "Zone") {
		tz = $2
		ruleUsed[$4] = 1
		if ($5 ~ /%/) rulePercentUsed[$4] = 1
	} else if ($1 == "Link" && zone_table == "zone.tab") {
		# Ignore Link commands if source and destination basenames
		# are identical, e.g. Europe/Istanbul versus Asia/Istanbul.
		src = $2
		dst = $3
		while ((i = index(src, "/"))) src = substr(src, i+1)
		while ((i = index(dst, "/"))) dst = substr(dst, i+1)
		if (src != dst) tz = $3
	} else if ($1 == "Rule") {
		ruleDefined[$2] = 1
		if ($10 != "-") ruleLetters[$2] = 1
	} else {
		ruleUsed[$2] = 1
		if ($3 ~ /%/) rulePercentUsed[$2] = 1
	}
	if (tz && tz ~ /\//) {
		if (!tztab[tz]) {
			printf "%s: no data for '%s'\n", zone_table, tz \
				>>"/dev/stderr"
			status = 1
		}
		zoneSeen[tz] = 1
	}
}

END {
	for (tz in ruleDefined) {
		if (!ruleUsed[tz]) {
			printf "%s: Rule never used\n", tz
			status = 1
		}
	}
	for (tz in ruleLetters) {
		if (!rulePercentUsed[tz]) {
			printf "%s: Rule contains letters never used\n", tz
			status = 1
		}
	}
	for (tz in tztab) {
		if (!zoneSeen[tz]) {
			printf "%s:%d: no Zone table for '%s'\n", \
				zone_table, tz2NR[tz], tz >>"/dev/stderr"
			status = 1
		}
	}
	if (0 < want_warnings) {
		for (cc in cc2name) {
			if (!cc_used[cc]) {
				printf "%s:%d: warning: " \
					"no Zone entries for %s (%s)\n", \
					iso_table, cc2NR[cc], cc, cc2name[cc]
			}
		}
	}

	exit status
}
