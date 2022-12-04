#
# Small script that refreshes the kernel feature support status in place.
#

for F_FILE in Documentation/features/*/*/arch-support.txt; do
	F=$(grep "^#         Kconfig:" "$F_FILE" | cut -c26-)

	#
	# Each feature F is identified by a pair (O, K), where 'O' can
	# be either the empty string (for 'nop') or "not" (the logical
	# negation operator '!'); other operators are not supported.
	#
	O=""
	K=$F
	if [[ "$F" == !* ]]; then
		O="not"
		K=$(echo $F | sed -e 's/^!//g')
	fi

	#
	# F := (O, K) is 'valid' iff there is a Kconfig file (for some
	# arch) which contains K.
	#
	# Notice that this definition entails an 'asymmetry' between
	# the case 'O = ""' and the case 'O = "not"'. E.g., F may be
	# _invalid_ if:
	#
	# [case 'O = ""']
	#   1) no arch provides support for F,
	#   2) K does not exist (e.g., it was renamed/mis-typed);
	#
	# [case 'O = "not"']
	#   3) all archs provide support for F,
	#   4) as in (2).
	#
	# The rationale for adopting this definition (and, thus, for
	# keeping the asymmetry) is:
	#
	#       We want to be able to 'detect' (2) (or (4)).
	#
	# (1) and (3) may further warn the developers about the fact
	# that K can be removed.
	#
	F_VALID="false"
	for ARCH_DIR in arch/*/; do
		K_FILES=$(find $ARCH_DIR -name "Kconfig*")
		K_GREP=$(grep "$K" $K_FILES)
		if [ ! -z "$K_GREP" ]; then
			F_VALID="true"
			break
		fi
	done
	if [ "$F_VALID" = "false" ]; then
		printf "WARNING: '%s' is not a valid Kconfig\n" "$F"
	fi

	T_FILE="$F_FILE.tmp"
	grep "^#" $F_FILE > $T_FILE
	echo "    -----------------------" >> $T_FILE
	echo "    |         arch |status|" >> $T_FILE
	echo "    -----------------------" >> $T_FILE
	for ARCH_DIR in arch/*/; do
		ARCH=$(echo $ARCH_DIR | sed -e 's/^arch//g' | sed -e 's/\///g')
		K_FILES=$(find $ARCH_DIR -name "Kconfig*")
		K_GREP=$(grep "$K" $K_FILES)
		#
		# Arch support status values for (O, K) are updated according
		# to the following rules.
		#
		#   - ("", K) is 'supported by a given arch', if there is a
		#     Kconfig file for that arch which contains K;
		#
		#   - ("not", K) is 'supported by a given arch', if there is
		#     no Kconfig file for that arch which contains K;
		#
		#   - otherwise: preserve the previous status value (if any),
		#                default to 'not yet supported'.
		#
		# Notice that, according these rules, invalid features may be
		# updated/modified.
		#
		if [ "$O" = "" ] && [ ! -z "$K_GREP" ]; then
			printf "    |%12s: |  ok  |\n" "$ARCH" >> $T_FILE
		elif [ "$O" = "not" ] && [ -z "$K_GREP" ]; then
			printf "    |%12s: |  ok  |\n" "$ARCH" >> $T_FILE
		else
			S=$(grep -v "^#" "$F_FILE" | grep " $ARCH:")
			if [ ! -z "$S" ]; then
				echo "$S" >> $T_FILE
			else
				printf "    |%12s: | TODO |\n" "$ARCH" \
					>> $T_FILE
			fi
		fi
	done
	echo "    -----------------------" >> $T_FILE
	mv $T_FILE $F_FILE
done
