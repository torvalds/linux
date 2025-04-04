#![allow(clippy::print_literal)]

fn main() {
    println!("cargo::rustc-env={}={}", "RUST_MODFILE", "This is only for rust-analyzer");
    println!("cargo::rustc-cfg={}", "kernel");
}
