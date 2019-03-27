#!/bin/sh
# $OpenBSD: mktestdata.sh,v 1.2 2017/04/30 23:33:48 djm Exp $

set -ex

cd testdata

rm -f rsa* dsa* ecdsa* ed25519*
rm -f known_hosts*

gen_all() {
	_n=$1
	_ecdsa_bits=256
	test "x$_n" = "x1" && _ecdsa_bits=384
	test "x$_n" = "x2" && _ecdsa_bits=521
	ssh-keygen -qt rsa -b 1024 -C "RSA #$_n" -N "" -f rsa_$_n
	ssh-keygen -qt dsa -b 1024 -C "DSA #$_n" -N "" -f dsa_$_n
	ssh-keygen -qt ecdsa -b $_ecdsa_bits -C "ECDSA #$_n" -N "" -f ecdsa_$_n
	ssh-keygen -qt ed25519 -C "ED25519 #$_n" -N "" -f ed25519_$_n
	# Don't need private keys
	rm -f rsa_$_n dsa_$_n ecdsa_$_n ed25519_$_n
}

hentries() {
	_preamble=$1
	_kspec=$2
	for k in `ls -1 $_kspec | sort` ; do
		printf "$_preamble "
		cat $k
	done
	echo
}

gen_all 1
gen_all 2
gen_all 3
gen_all 4
gen_all 5
gen_all 6

# A section of known_hosts with hashed hostnames.
(
	hentries "sisyphus.example.com" "*_5.pub"
	hentries "prometheus.example.com,192.0.2.1,2001:db8::1" "*_6.pub"
) > known_hosts_hash_frag
ssh-keygen -Hf known_hosts_hash_frag
rm -f known_hosts_hash_frag.old

# Populated known_hosts, including comments, hashed names and invalid lines
(
	echo "# Plain host keys, plain host names"
	hentries "sisyphus.example.com" "*_1.pub"

	echo "# Plain host keys, hostnames + addresses"
	hentries "prometheus.example.com,192.0.2.1,2001:db8::1" "*_2.pub"

	echo "# Some hosts with wildcard names / IPs"
	hentries "*.example.com,192.0.2.*,2001:*" "*_3.pub"

	echo "# Hashed hostname and address entries"
	cat known_hosts_hash_frag
	rm -f known_hosts_hash_frag
	echo

	echo "# Revoked and CA keys"
	printf "@revoked sisyphus.example.com " ; cat ed25519_4.pub
	printf "@cert-authority prometheus.example.com " ; cat ecdsa_4.pub
	printf "@cert-authority *.example.com " ; cat dsa_4.pub

	printf "\n"
	echo "# Some invalid lines"
	# Invalid marker
	printf "@what sisyphus.example.com " ; cat dsa_1.pub
	# Key missing
	echo "sisyphus.example.com      "
	# Key blob missing
	echo "prometheus.example.com ssh-ed25519 "
	# Key blob truncated
	echo "sisyphus.example.com ssh-dsa AAAATgAAAAdz"
	# Invalid type
	echo "sisyphus.example.com ssh-XXX AAAATgAAAAdzc2gtWFhYAAAAP0ZVQ0tPRkZGVUNLT0ZGRlVDS09GRkZVQ0tPRkZGVUNLT0ZGRlVDS09GRkZVQ0tPRkZGVUNLT0ZGRlVDS09GRg=="
	# Type mismatch with blob
	echo "prometheus.example.com ssh-rsa AAAATgAAAAdzc2gtWFhYAAAAP0ZVQ0tPRkZGVUNLT0ZGRlVDS09GRkZVQ0tPRkZGVUNLT0ZGRlVDS09GRkZVQ0tPRkZGVUNLT0ZGRlVDS09GRg=="
) > known_hosts

echo OK
