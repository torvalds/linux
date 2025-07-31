#!/usr/bin/env python3
"""
Test client for HER OS Metadata Daemon.
Usage:
  python3 test_client.py 'COMMAND'
Example:
  python3 test_client.py 'ADD_FILE /tmp/foo.txt 5 12345 abcdef'
  python3 test_client.py 'GET_FILE /tmp/foo.txt'
  python3 test_client.py 'ADD_TAG /tmp/foo.txt project Alpha'
  python3 test_client.py 'GET_TAGS /tmp/foo.txt'
"""
import sys
import socket

SOCKET_PATH = "/tmp/heros_metadata.sock"

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 test_client.py 'COMMAND'", file=sys.stderr)
        sys.exit(1)
    cmd = sys.argv[1]
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.connect(SOCKET_PATH)
        client.sendall(cmd.encode("utf-8"))
        resp = client.recv(4096)
        print(resp.decode("utf-8").strip())

if __name__ == "__main__":
    main() 