#!/bin/sh

CLI=wpa_cli

pbc()
{
	echo "Starting PBC mode"
	echo "Push button on the station within two minutes"
	if ! $CLI wps_pbc | grep -q OK; then
		echo "Failed to enable PBC mode"
	fi
}

enter_pin()
{
	echo "Enter a PIN from a station to be enrolled to the network."
	printf "Enrollee PIN: "
	read pin
	cpin=`$CLI wps_check_pin "$pin" | tail -1`
	if [ "$cpin" = "FAIL-CHECKSUM" ]; then
		echo "Checksum digit is not valid"
		printf "Do you want to use this PIN (y/n)? "
		read resp
		case "$resp" in
			y*)
				cpin=`echo "$pin" | sed "s/[^1234567890]//g"`
				;;
			*)
				return 1
				;;
		esac
	fi
	if [ "$cpin" = "FAIL" ]; then
		echo "Invalid PIN: $pin"
		return 1
	fi
	echo "Enabling Enrollee PIN: $cpin"
	$CLI wps_pin any "$cpin"
}

show_config()
{
	$CLI status wps
}

main_menu()
{
	echo "WPS AP"
	echo "------"
	echo "1: Push button (activate PBC)"
	echo "2: Enter Enrollee PIN"
	echo "3: Show current configuration"
	echo "0: Exit wps-ap-cli"

	printf "Command: "
	read cmd

	case "$cmd" in
		1)
			pbc
			;;
		2)
			enter_pin
			;;
		3)
			show_config
			;;
		0)
			exit 0
			;;
		*)
			echo "Unknown command: $cmd"
			;;
	esac

	echo
	main_menu
}


main_menu
