#
# delete duplicate lines
#
{
	if (old != $0)
		printf "%s\n", $0
	old = $0
}
