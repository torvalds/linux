use clap::{Parser, Subcommand};
use std::os::unix::net::UnixStream;
use std::io::{Write, BufRead, BufReader};

const SOCKET_PATH: &str = "/tmp/heros_metadata.sock";

/// CLI tool for inspecting and overriding application manifests via the Metadata Daemon
#[derive(Parser)]
#[command(name = "manifest_cli")]
#[command(about = "Inspect and override application manifests", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

/// Supported subcommands for the manifest CLI
#[derive(Subcommand)]
enum Commands {
    /// Inspect the manifest for an application
    Inspect {
        /// Application name
        app: String,
    },
    /// Override the manifest for an application
    Override {
        /// Application name
        app: String,
        /// Manifest JSON string
        json: String,
    },
    /// Show the dependency graph for an application
    DepGraph {
        /// Application name
        app: String,
    },
}

/// Send a command to the metadata daemon via Unix socket and return the response.
fn send_command(command: &str) -> std::io::Result<String> {
    let mut stream = UnixStream::connect(SOCKET_PATH)?;
    stream.write_all(command.as_bytes())?;
    stream.write_all(b"\n")?;
    let mut reader = BufReader::new(stream);
    let mut response = String::new();
    reader.read_line(&mut response)?;
    Ok(response.trim_end().to_string())
}

/// Pretty-print a JSON string if possible, otherwise print as-is.
fn print_json_pretty(raw: &str) {
    match serde_json::from_str::<serde_json::Value>(raw) {
        Ok(json) => {
            println!("{}", serde_json::to_string_pretty(&json).unwrap_or_else(|_| raw.to_string()));
        }
        Err(_) => println!("{}", raw),
    }
}

/// Handle the 'inspect' subcommand: fetch and pretty-print the manifest for an app.
fn handle_inspect(app: &str) {
    let cmd = format!("MANIFEST_GET {}", app);
    match send_command(&cmd) {
        Ok(resp) => {
            if resp.starts_with("RESULT: ") {
                let json = &resp[8..];
                print_json_pretty(json);
            } else {
                eprintln!("Error: {}", resp);
            }
        }
        Err(e) => eprintln!("Socket error: {}", e),
    }
}

/// Handle the 'override' subcommand: set a new manifest for an app.
fn handle_override(app: &str, json: &str) {
    let cmd = format!("MANIFEST_SET {} {}", app, json);
    match send_command(&cmd) {
        Ok(resp) => {
            if resp == "OK" {
                println!("Manifest for '{}' updated successfully.", app);
            } else {
                eprintln!("Error: {}", resp);
            }
        }
        Err(e) => eprintln!("Socket error: {}", e),
    }
}

/// Handle the 'dep-graph' subcommand: fetch and pretty-print the dependency graph for an app.
fn handle_dep_graph(app: &str) {
    let cmd = format!("MANIFEST_DEP_GRAPH {}", app);
    match send_command(&cmd) {
        Ok(resp) => {
            if resp.starts_with("RESULT: ") {
                let json = &resp[8..];
                print_json_pretty(json);
            } else {
                eprintln!("Error: {}", resp);
            }
        }
        Err(e) => eprintln!("Socket error: {}", e),
    }
}

/// Entry point: parse CLI arguments and dispatch to the appropriate handler.
fn main() {
    let cli = Cli::parse();
    match &cli.command {
        Commands::Inspect { app } => handle_inspect(app),
        Commands::Override { app, json } => handle_override(app, json),
        Commands::DepGraph { app } => handle_dep_graph(app),
    }
} 